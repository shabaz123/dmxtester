/************************************************************************
  * dmxtester
 * rev 1 - July 2026 - shabaz
 *
 * Core 0: USB/serial command-line interface
 * Core 1: DMX transmitter, fixed at 40 Hz (one frame every 25 ms)
 *
 * Hardware connections:
 *   GPIO 0  - DMX UART/PIO output
 *   GPIO 1  - DMX UART/PIO input (not used)
 *   GPIO 2  - RS-485 transmit/receive direction control
 *   GPIO 12 - status LED
 ************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hardware/gpio.h"
#include "hardware/sync.h"
#include "pico/critical_section.h"
#include "pico/multicore.h"
#include "pico/stdlib.h"
#include "pico/stdio.h"
#include "Pico-DMX/src/DmxOutput.h"

// ---------------------------------------------------------------------
// Hardware configuration
// ---------------------------------------------------------------------
constexpr uint LED_PIN = 12;
constexpr uint DMX_PIN = 0;
constexpr uint TX_NRX_PIN = 2;

constexpr size_t DMX_CHANNEL_COUNT = 512;
constexpr uint32_t DMX_FRAME_RATE_HZ = 40;
constexpr uint32_t DMX_FRAME_PERIOD_US = 1000000u / DMX_FRAME_RATE_HZ;

// DMX slot zero is the start code. Channels are slots 1..512.
static uint8_t universe[DMX_CHANNEL_COUNT + 1] = {0};
static critical_section_t universe_lock;

static DmxOutput dmx;
static volatile uint32_t frames_sent = 0;
static volatile uint32_t missed_deadlines = 0;

// ---------------------------------------------------------------------
// Utility functions
// ---------------------------------------------------------------------
static void print_title() {
    printf("\nEasyDMX 40 Hz controller\n");
    printf("Built %s %s\n", __DATE__, __TIME__);
    printf("DMX: %u channels, %u frames/second\n\n",
           static_cast<unsigned>(DMX_CHANNEL_COUNT),
           static_cast<unsigned>(DMX_FRAME_RATE_HZ));
}

static void print_help() {
    printf("Commands:\n");
    printf("  help                         Show this command list\n");
    printf("  set_chan <channel> <value>   Set channel 1..512 to 0..255\n");
    printf("  get_chan <channel>           Read channel 1..512\n");
    printf("  set_all <value>              Set all channels to 0..255\n");
    printf("  set_rgb <first> <r> <g> <b>  Set three consecutive channels\n");
    printf("  blackout                     Set all channels to zero\n");
    printf("  status                       Show DMX transmitter statistics\n");
    printf("  $<command>                   Machine mode: OK/ERR response only\n");
    printf("\nExamples:\n");
    printf("  set_chan 1 255\n");
    printf("  set_all 128\n");
    printf("  set_rgb 1 255 40 0\n\n");
}

static bool parse_integer(const char *text, int min_value, int max_value,
                          int *result) {
    if (text == nullptr || *text == '\0') {
        return false;
    }

    char *end = nullptr;
    const long value = strtol(text, &end, 0);

    if (*end != '\0' || value < min_value || value > max_value) {
        return false;
    }

    *result = static_cast<int>(value);
    return true;
}

static void set_channel(unsigned channel, uint8_t value) {
    critical_section_enter_blocking(&universe_lock);
    universe[channel] = value;
    critical_section_exit(&universe_lock);
}

static uint8_t get_channel(unsigned channel) {
    critical_section_enter_blocking(&universe_lock);
    const uint8_t value = universe[channel];
    critical_section_exit(&universe_lock);
    return value;
}

static void set_all_channels(uint8_t value) {
    critical_section_enter_blocking(&universe_lock);
    memset(&universe[1], value, DMX_CHANNEL_COUNT);
    critical_section_exit(&universe_lock);
}

static void set_rgb(unsigned first_channel, uint8_t red, uint8_t green,
                    uint8_t blue) {
    critical_section_enter_blocking(&universe_lock);
    universe[first_channel] = red;
    universe[first_channel + 1] = green;
    universe[first_channel + 2] = blue;
    critical_section_exit(&universe_lock);
}

// ---------------------------------------------------------------------
// Core 1: fixed-rate DMX output
// ---------------------------------------------------------------------
static void dmx_core() {
    uint8_t frame[DMX_CHANNEL_COUNT + 1] = {0};

    gpio_init(TX_NRX_PIN);
    gpio_set_dir(TX_NRX_PIN, GPIO_OUT);
    gpio_put(TX_NRX_PIN, 1);  // RS-485 transmit mode

    dmx.begin(DMX_PIN);

    absolute_time_t next_frame = get_absolute_time();

    while (true) {
        next_frame = delayed_by_us(next_frame, DMX_FRAME_PERIOD_US);

        // Take a coherent snapshot. The lock is held only for a short memcpy;
        // the slow DMX transfer happens after the lock is released.
        critical_section_enter_blocking(&universe_lock);
        memcpy(frame, universe, sizeof(frame));
        critical_section_exit(&universe_lock);

        dmx.write(frame, DMX_CHANNEL_COUNT);
        while (dmx.busy()) {
            tight_loop_contents();
        }
        ++frames_sent;

        const int64_t remaining_us = absolute_time_diff_us(get_absolute_time(),
                                                           next_frame);
        if (remaining_us > 0) {
            sleep_until(next_frame);
        } else {
            // The transfer finished after its intended deadline. Rebase the
            // schedule so that a delay does not cause a burst of catch-up frames.
            ++missed_deadlines;
            next_frame = get_absolute_time();
        }
    }
}

// ---------------------------------------------------------------------
// Core 0: command parser
// ---------------------------------------------------------------------
enum class CommandResult {
    Ok,
    UnknownCommand,
    BadParameters,
    OutOfRange
};

static void machine_reply(CommandResult result) {
    switch (result) {
        case CommandResult::Ok:
            printf("OK\r\n");
            break;
        case CommandResult::UnknownCommand:
            printf("ERR COMMAND\r\n");
            break;
        case CommandResult::BadParameters:
            printf("ERR PARAMS\r\n");
            break;
        case CommandResult::OutOfRange:
            printf("ERR RANGE\r\n");
            break;
    }
    fflush(stdout);
}

static CommandResult process_command(char *line, bool machine_mode) {
    constexpr int MAX_ARGUMENTS = 5;
    char *argv[MAX_ARGUMENTS] = {nullptr};
    int argc = 0;

    char *token = strtok(line, " \t\r\n");
    while (token != nullptr && argc < MAX_ARGUMENTS) {
        argv[argc++] = token;
        token = strtok(nullptr, " \t\r\n");
    }

    // A line containing only '$' is a machine-mode connection test.
    if (argc == 0) {
        return CommandResult::Ok;
    }

    if (strcmp(argv[0], "help") == 0 || strcmp(argv[0], "?") == 0) {
        if (argc != 1) {
            if (!machine_mode) printf("Usage: help\n");
            return CommandResult::BadParameters;
        }
        if (!machine_mode) print_help();
        return CommandResult::Ok;
    }

    if (strcmp(argv[0], "set_chan") == 0) {
        if (argc != 3) {
            if (!machine_mode) printf("Usage: set_chan <1..512> <0..255>\n");
            return CommandResult::BadParameters;
        }
        int channel = 0;
        int value = 0;
        if (!parse_integer(argv[1], 1, DMX_CHANNEL_COUNT, &channel) ||
            !parse_integer(argv[2], 0, 255, &value)) {
            if (!machine_mode) printf("Channel must be 1..512 and value 0..255\n");
            return CommandResult::OutOfRange;
        }
        set_channel(static_cast<unsigned>(channel), static_cast<uint8_t>(value));
        if (!machine_mode) printf("Channel %d = %d\n", channel, value);
        return CommandResult::Ok;
    }

    if (strcmp(argv[0], "get_chan") == 0) {
        if (argc != 2) {
            if (!machine_mode) printf("Usage: get_chan <1..512>\n");
            return CommandResult::BadParameters;
        }
        int channel = 0;
        if (!parse_integer(argv[1], 1, DMX_CHANNEL_COUNT, &channel)) {
            if (!machine_mode) printf("Channel must be 1..512\n");
            return CommandResult::OutOfRange;
        }
        const unsigned value = static_cast<unsigned>(get_channel(channel));
        if (machine_mode) {
            printf("OK %u\r\n", value);
            fflush(stdout);
        } else {
            printf("Channel %d = %u\n", channel, value);
        }
        // get_chan emits its own machine response because it returns data.
        return CommandResult::Ok;
    }

    if (strcmp(argv[0], "set_all") == 0) {
        if (argc != 2) {
            if (!machine_mode) printf("Usage: set_all <0..255>\n");
            return CommandResult::BadParameters;
        }
        int value = 0;
        if (!parse_integer(argv[1], 0, 255, &value)) {
            if (!machine_mode) printf("Value must be 0..255\n");
            return CommandResult::OutOfRange;
        }
        set_all_channels(static_cast<uint8_t>(value));
        if (!machine_mode) printf("All channels = %d\n", value);
        return CommandResult::Ok;
    }

    if (strcmp(argv[0], "set_rgb") == 0) {
        if (argc != 5) {
            if (!machine_mode) printf("Usage: set_rgb <first channel 1..510> <r> <g> <b>\n");
            return CommandResult::BadParameters;
        }
        int first = 0, red = 0, green = 0, blue = 0;
        if (!parse_integer(argv[1], 1, DMX_CHANNEL_COUNT - 2, &first) ||
            !parse_integer(argv[2], 0, 255, &red) ||
            !parse_integer(argv[3], 0, 255, &green) ||
            !parse_integer(argv[4], 0, 255, &blue)) {
            if (!machine_mode) printf("First channel must be 1..510; RGB values 0..255\n");
            return CommandResult::OutOfRange;
        }
        set_rgb(static_cast<unsigned>(first), static_cast<uint8_t>(red),
                static_cast<uint8_t>(green), static_cast<uint8_t>(blue));
        if (!machine_mode) {
            printf("Channels %d..%d = %d, %d, %d\n", first, first + 2,
                   red, green, blue);
        }
        return CommandResult::Ok;
    }

    if (strcmp(argv[0], "blackout") == 0) {
        if (argc != 1) {
            if (!machine_mode) printf("Usage: blackout\n");
            return CommandResult::BadParameters;
        }
        set_all_channels(0);
        if (!machine_mode) printf("Blackout enabled\n");
        return CommandResult::Ok;
    }

    if (strcmp(argv[0], "status") == 0) {
        if (argc != 1) {
            if (!machine_mode) printf("Usage: status\n");
            return CommandResult::BadParameters;
        }
        if (machine_mode) {
            printf("OK %lu %lu %u\r\n",
                   static_cast<unsigned long>(frames_sent),
                   static_cast<unsigned long>(missed_deadlines),
                   static_cast<unsigned>(DMX_FRAME_RATE_HZ));
            fflush(stdout);
        } else {
            printf("Frames sent: %lu, missed deadlines: %lu, rate: %u Hz\n",
                   static_cast<unsigned long>(frames_sent),
                   static_cast<unsigned long>(missed_deadlines),
                   static_cast<unsigned>(DMX_FRAME_RATE_HZ));
        }
        return CommandResult::Ok;
    }

    if (!machine_mode) {
        printf("Unknown command: %s\n", argv[0]);
        printf("Type 'help' for a command list.\n");
    }
    return CommandResult::UnknownCommand;
}

// ---------------------------------------------------------------------
// Interactive line editor
// ---------------------------------------------------------------------
// Change this definition to retain more or fewer commands.
#define COMMAND_HISTORY_DEPTH 20

constexpr size_t COMMAND_LINE_LENGTH = 128;
constexpr const char *COMMAND_PROMPT = "dmx> ";

struct LineEditor {
    char line[COMMAND_LINE_LENGTH] = {0};
    size_t length = 0;
    size_t cursor = 0;

    char history[COMMAND_HISTORY_DEPTH][COMMAND_LINE_LENGTH] = {{0}};
    size_t history_count = 0;
    size_t history_next = 0;

    // -1 means editing the current line. Zero means the newest history item,
    // one means the next older item, and so on.
    int history_offset = -1;
    char saved_line[COMMAND_LINE_LENGTH] = {0};
    size_t saved_length = 0;

    enum class EscapeState {
        None,
        Escape,
        Csi
    } escape_state = EscapeState::None;

    bool ignore_next_lf = false;
    bool machine_mode = false;
};

static void redraw_line(const LineEditor &editor) {
    // Return to the start, print the prompt and complete line, then erase any
    // remnants of a previously longer line.
    printf("\r%s%s\x1b[K", COMMAND_PROMPT, editor.line);

    // Printing leaves the terminal cursor at the end. Move it back to the
    // editor's logical cursor position when editing in the middle of a line.
    const size_t move_left = editor.length - editor.cursor;
    if (move_left > 0) {
        printf("\x1b[%uD", static_cast<unsigned>(move_left));
    }
    fflush(stdout);
}

static void load_editor_line(LineEditor &editor, const char *text) {
    strncpy(editor.line, text, sizeof(editor.line) - 1);
    editor.line[sizeof(editor.line) - 1] = '\0';
    editor.length = strlen(editor.line);
    editor.cursor = editor.length;
    redraw_line(editor);
}

static void add_history(LineEditor &editor, const char *line) {
    if (line[0] == '\0') {
        return;
    }

    // Do not add an immediate duplicate of the most recent command.
    if (editor.history_count > 0) {
        const size_t newest =
            (editor.history_next + COMMAND_HISTORY_DEPTH - 1) %
            COMMAND_HISTORY_DEPTH;
        if (strcmp(editor.history[newest], line) == 0) {
            return;
        }
    }

    strncpy(editor.history[editor.history_next], line,
            COMMAND_LINE_LENGTH - 1);
    editor.history[editor.history_next][COMMAND_LINE_LENGTH - 1] = '\0';

    editor.history_next =
        (editor.history_next + 1) % COMMAND_HISTORY_DEPTH;
    if (editor.history_count < COMMAND_HISTORY_DEPTH) {
        ++editor.history_count;
    }
}

static const char *history_item(const LineEditor &editor, size_t offset) {
    const size_t index =
        (editor.history_next + COMMAND_HISTORY_DEPTH - 1 - offset) %
        COMMAND_HISTORY_DEPTH;
    return editor.history[index];
}

static void history_up(LineEditor &editor) {
    if (editor.history_count == 0) {
        putchar('\a');
        return;
    }

    if (editor.history_offset < 0) {
        strncpy(editor.saved_line, editor.line, sizeof(editor.saved_line) - 1);
        editor.saved_line[sizeof(editor.saved_line) - 1] = '\0';
        editor.saved_length = editor.length;
        editor.history_offset = 0;
    } else if (static_cast<size_t>(editor.history_offset + 1) <
               editor.history_count) {
        ++editor.history_offset;
    } else {
        putchar('\a');
        return;
    }

    load_editor_line(editor,
                     history_item(editor,
                                  static_cast<size_t>(editor.history_offset)));
}

static void history_down(LineEditor &editor) {
    if (editor.history_offset < 0) {
        putchar('\a');
        return;
    }

    if (editor.history_offset > 0) {
        --editor.history_offset;
        load_editor_line(
            editor,
            history_item(editor, static_cast<size_t>(editor.history_offset)));
    } else {
        editor.history_offset = -1;
        load_editor_line(editor, editor.saved_line);
        editor.length = editor.saved_length;
        editor.cursor = editor.length;
    }
}

static void insert_character(LineEditor &editor, char c) {
    if (editor.length >= sizeof(editor.line) - 1) {
        putchar('\a');
        return;
    }

    memmove(&editor.line[editor.cursor + 1], &editor.line[editor.cursor],
            editor.length - editor.cursor + 1);
    editor.line[editor.cursor] = c;
    ++editor.cursor;
    ++editor.length;
    redraw_line(editor);
}

static void erase_character_before_cursor(LineEditor &editor) {
    if (editor.cursor == 0) {
        putchar('\a');
        return;
    }

    memmove(&editor.line[editor.cursor - 1], &editor.line[editor.cursor],
            editor.length - editor.cursor + 1);
    --editor.cursor;
    --editor.length;
    redraw_line(editor);
}

// Reads and edits one command line using ANSI cursor-key sequences commonly
// emitted by USB serial terminals: ESC [ A/B/C/D for up/down/right/left.
static bool read_command_line(LineEditor &editor, char *completed_line,
                              size_t completed_size,
                              bool *completed_machine_mode) {
    const int c = getchar_timeout_us(1000);
    if (c == PICO_ERROR_TIMEOUT) {
        return false;
    }

    // Machine commands are deliberately plain lines. Do not interpret cursor
    // keys or echo any received characters after a leading '$'.
    if (!editor.machine_mode) {
        if (editor.escape_state == LineEditor::EscapeState::Escape) {
            editor.escape_state =
                (c == '[') ? LineEditor::EscapeState::Csi
                           : LineEditor::EscapeState::None;
            return false;
        }

        if (editor.escape_state == LineEditor::EscapeState::Csi) {
            editor.escape_state = LineEditor::EscapeState::None;
            switch (c) {
                case 'A': history_up(editor); break;
                case 'B': history_down(editor); break;
                case 'C':
                    if (editor.cursor < editor.length) {
                        ++editor.cursor;
                        printf("\x1b[C");
                    } else putchar('\a');
                    break;
                case 'D':
                    if (editor.cursor > 0) {
                        --editor.cursor;
                        printf("\x1b[D");
                    } else putchar('\a');
                    break;
                default: break;
            }
            fflush(stdout);
            return false;
        }

        if (c == 0x1b) {
            editor.escape_state = LineEditor::EscapeState::Escape;
            return false;
        }
    }

    if (c == '\n' && editor.ignore_next_lf) {
        editor.ignore_next_lf = false;
        return false;
    }
    editor.ignore_next_lf = false;

    if (c == '\r' || c == '\n') {
        if (c == '\r') editor.ignore_next_lf = true;

        if (!editor.machine_mode) printf("\r\n");
        editor.line[editor.length] = '\0';
        strncpy(completed_line, editor.line, completed_size - 1);
        completed_line[completed_size - 1] = '\0';
        *completed_machine_mode = editor.machine_mode;

        if (!editor.machine_mode) add_history(editor, completed_line);

        editor.line[0] = '\0';
        editor.length = 0;
        editor.cursor = 0;
        editor.history_offset = -1;
        editor.saved_line[0] = '\0';
        editor.saved_length = 0;
        editor.machine_mode = false;
        return true;
    }

    // A '$' is a protocol marker only when it is the first character.
    if (!editor.machine_mode && editor.length == 0 && c == '$') {
        editor.machine_mode = true;
        return false;
    }

    if (c == '\b' || c == 0x7f) {
        if (editor.machine_mode) {
            if (editor.length > 0) {
                --editor.length;
                editor.cursor = editor.length;
                editor.line[editor.length] = '\0';
            }
        } else {
            erase_character_before_cursor(editor);
        }
        return false;
    }

    if (c >= 0x20 && c <= 0x7e) {
        if (editor.machine_mode) {
            if (editor.length < sizeof(editor.line) - 1) {
                editor.line[editor.length++] = static_cast<char>(c);
                editor.line[editor.length] = '\0';
                editor.cursor = editor.length;
            }
        } else {
            editor.history_offset = -1;
            insert_character(editor, static_cast<char>(c));
        }
    }

    return false;
}

int main() {
    stdio_init_all();
    sleep_ms(1500);

    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);
    gpio_put(LED_PIN, 1);

    critical_section_init(&universe_lock);
    universe[0] = 0;  // DMX start code
    set_all_channels(0);

    print_title();
    print_help();

    multicore_launch_core1(dmx_core);

    char line[COMMAND_LINE_LENGTH] = {0};
    LineEditor editor;

    printf("%s", COMMAND_PROMPT);
    fflush(stdout);

    while (true) {
        bool machine_mode = false;
        if (!read_command_line(editor, line, sizeof(line), &machine_mode)) {
            tight_loop_contents();
            continue;
        }

        const CommandResult result = process_command(line, machine_mode);
        // gpio_xor_mask(1u << LED_PIN); // toggle the LED

        // get_chan and status include returned data and therefore generate
        // their own machine-mode OK response.
        const bool command_has_machine_data =
            machine_mode &&
            (strncmp(line, "get_chan", 8) == 0 ||
             strncmp(line, "status", 6) == 0) &&
            result == CommandResult::Ok;

        if (machine_mode) {
            if (!command_has_machine_data) machine_reply(result);
        } else {
            printf("%s", COMMAND_PROMPT);
            fflush(stdout);
        }
    }
}
