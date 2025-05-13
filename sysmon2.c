#include <stdio.h>
#include <stdlib.h>
#include <ncurses.h>
#include <unistd.h>
#include <string.h>
#define MAX_PROCESSES 256

typedef struct {
    int pid;
    char user[32];
    float cpu, mem;
    char command[256];
    int nice;
    int priority;

} Process;

Process proc_list[MAX_PROCESSES];
int proc_count = 0;
int scroll_offset = 0;
int matched_proc_count = 0;

char search_term[64] = "";
long long last_user = 0, last_nice = 0, last_system = 0, last_idle = 0;

void init_colors() {
    start_color();
    use_default_colors();
    init_pair(1, COLOR_YELLOW, -1);
    init_pair(2, COLOR_CYAN, -1);
    init_pair(3, COLOR_GREEN, -1);
    init_pair(4, COLOR_RED, -1);
    init_pair(5, COLOR_BLUE, -1);
    init_pair(1, COLOR_WHITE, COLOR_BLACK);
    init_pair(2, COLOR_GREEN, COLOR_BLACK);
    init_pair(3, COLOR_YELLOW, COLOR_BLACK);
    init_pair(4, COLOR_RED, COLOR_BLACK);

}

float get_cpu_usage() {
    FILE *fp = fopen("/proc/stat", "r");
    if (!fp) return 0;

    char buffer[256];
    fgets(buffer, sizeof(buffer), fp);
    fclose(fp);

    long long user, nice, system, idle;
    sscanf(buffer, "cpu %lld %lld %lld %lld", &user, &nice, &system, &idle);

    long long total_diff = (user - last_user) + (nice - last_nice) + (system - last_system);
    long long total_time = total_diff + (idle - last_idle);

    float cpu = (total_time == 0) ? 0 : (100.0 * total_diff / total_time);

    last_user = user;
    last_nice = nice;
    last_system = system;
    last_idle = idle;

    return cpu;
}

float get_memory_usage() {
    FILE *fp = fopen("/proc/meminfo", "r");
    if (!fp) return 0;

    char label[32];
    long total = 0, free = 0, buffers = 0, cached = 0;
    while (fscanf(fp, "%31s %ld", label, &total) == 2) {
        if (strcmp(label, "MemTotal:") == 0) total = total;
        else if (strcmp(label, "MemFree:") == 0) free = total;
        else if (strcmp(label, "Buffers:") == 0) buffers = total;
        else if (strcmp(label, "Cached:") == 0) cached = total;
        if (cached && buffers && free) break;
    }
    fclose(fp);

    long used = total - free - buffers - cached;
    return (float)used / total * 100;
}

void draw_bar(int row, const char *label, float percent) {
    attron(COLOR_PAIR(2));
    mvprintw(row, 0, "%s", label);
    attroff(COLOR_PAIR(2));

    int width = COLS - 15;
    int filled = (int)(percent / 100.0 * width);
    attron(COLOR_PAIR(5));
    mvprintw(row, 10, "[");
    for (int i = 0; i < width; i++) {
        if (i < filled) addch('=');
        else addch(' ');
    }
    printw("] %.1f%%", percent);
    attroff(COLOR_PAIR(5));
}

void draw_usage_bar(int y, float percent) {
    int bar_width = 20;
    int filled = (int)(percent / 100.0 * bar_width);

    for (int i = 0; i < bar_width; i++) {
        if (i < filled) {
            if (percent < 30)
                attron(COLOR_PAIR(2)); // green
            else if (percent < 70)
                attron(COLOR_PAIR(3)); // yellow
            else
                attron(COLOR_PAIR(4)); // red
        } else {
            attron(COLOR_PAIR(1)); // dim gray
        }
        mvprintw(y, 70 + i, "|");
        attroff(COLOR_PAIR(1) | COLOR_PAIR(2) | COLOR_PAIR(3) | COLOR_PAIR(4));
    }
}

void head() {
    attron(COLOR_PAIR(1) | A_BOLD);
    mvprintw(3, 0, "  PID  USER       NI PRI  CPU%%   MEM%% COMMAND");
    attroff(COLOR_PAIR(1) | A_BOLD);
    mvprintw(4, 0, "--------------------------------------------");
}

void process_line(int row, int pid, const char* user, int nice, int pri, float cpu, float mem, const char* command) {
    mvprintw(row, 1, "%5d %-8s %3d %3d %5.1f%% %5.1f%% %-20.20s", 
             pid, user, nice, pri, cpu, mem, command);
    draw_usage_bar(row, cpu);
}

void fetch_processes() {
    proc_count = 0;

    FILE *fp = popen("ps -eo pid,user,ni,pri,pcpu,pmem,comm --sort=-%cpu", "r");
    if (!fp) return;

    char buffer[512];
    fgets(buffer, sizeof(buffer), fp);  // Skip header

    while (fgets(buffer, sizeof(buffer), fp) && proc_count < MAX_PROCESSES) {
        Process p;
       if (sscanf(buffer, "%d %31s %d %d %f %f %255[^\n]", &p.pid, p.user, &p.nice, &p.priority, &p.cpu, &p.mem, p.command) == 7)
{
    proc_list[proc_count++] = p;
}
	{
            proc_list[proc_count++] = p;
        }
    }

    pclose(fp);
}

void display_processes() {
    int row = 5;
    int shown = 0;
    int available_rows = LINES - 7;
    matched_proc_count=0;

    for (int i = 0; i < proc_count; i++) {
        if (strlen(search_term) == 0 || strstr(proc_list[i].command, search_term)) {
            matched_proc_count++;
        }
    }
    int skipped = 0;
    for (int i = 0; i < proc_count && shown < available_rows; i++) {
        if (strlen(search_term) == 0 || strstr(proc_list[i].command, search_term)) {
            if (skipped < scroll_offset) {
                skipped++;
                continue;
            }
            process_line(row++, proc_list[i].pid, proc_list[i].user,
                         proc_list[i].nice, proc_list[i].priority,
                         proc_list[i].cpu, proc_list[i].mem,
                         proc_list[i].command);
            shown++;
        }
    }
    if (strlen(search_term) > 0){
        mvprintw(LINES - 3, 2, "Filter: %s", search_term);
    }
}

void footer() {
    attron(A_DIM);
    mvprintw(LINES - 2, 0, "[q] Quit    [Up/Down] Scroll    [Updated every 1s]");
    attroff(A_DIM);
}

int main() {
    initscr();
    keypad(stdscr, TRUE);
    noecho();
    cbreak();
    curs_set(0);
    nodelay(stdscr, TRUE);

    init_colors();

    while (1) {
        clear();

        float cpu = get_cpu_usage();
        float mem = get_memory_usage();

        draw_bar(0, "CPU Usage", cpu);
        draw_bar(1, "RAM Usage", mem);

        head();
        fetch_processes();
	display_processes();
        footer();
        refresh();

        int ch = getch();
	if (ch == 'q') break;
	else if (ch == '/') {
		echo();
                curs_set(1);
                mvprintw(LINES - 2, 2, "Search: ");
                getnstr(search_term, sizeof(search_term) - 1);
                noecho();
                curs_set(0);
                scroll_offset = 0;
	}
	else if (ch == KEY_DOWN) {
		int max_scroll = matched_proc_count - (LINES - 7);
		if (scroll_offset < max_scroll)
			scroll_offset++;
	}
        else if (ch == KEY_UP && scroll_offset > 0) {
                scroll_offset--;
        }

        usleep(1000000);
    }

    endwin();
    return 0;
}

