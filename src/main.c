#include "ui/ui.h"
#include <string.h>

#include "core/core.h"
#include "ui/plot.h"

int main(void) {
    ui_t ui;
    if (!ui_init(&ui)) return 1;
    core_init();

    char line[1024] = "";
    while (ui_readline(&ui, line, (int)sizeof(line))) {
    //while (true) {
        if (line[0] == 0) continue;
        if (line[0] == ':') {
            cmd_try_exec(ui.cmds, &ui, line);
        } else {
            core_result_t r = core_eval(line);

            switch (r.kind) {
                case CORE_EVALUATION:
                    ui_print(&ui, "%s = %s\n", line, r.text);
                    break;
                case CORE_ASSIGNMENT:
                case CORE_PLOT:
                case CORE_TABLE:
                    ui_print(&ui, "%s\n", line);
                    break;
                case CORE_ERROR:
                    ui_print(&ui, "%s\n", r.text);
                    break;
            }
        }
        ui_refresh(&ui);
        if (ui.should_close) break;
    }

    ui_shutdown(&ui);
    core_shutdown();
    return 0;
}
