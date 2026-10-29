/* NovaOS — маленькая учебная операционная система.
 * Работает в 32-битном защищённом режиме, без библиотек:
 * сама рисует текст на экране, читает клавиатуру и считает время. */

typedef unsigned char  u8;
typedef unsigned short u16;
typedef unsigned int   u32;

/* ---------- Порты ввода-вывода ---------- */

static inline void outb(u16 port, u8 val) { __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port)); }
static inline u8 inb(u16 port) { u8 r; __asm__ volatile("inb %1, %0" : "=a"(r) : "Nd"(port)); return r; }

/* ---------- Строки ---------- */

static int str_len(const char *s) { int n = 0; while (s[n]) n++; return n; }

static int str_eq(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

static int starts_with(const char *s, const char *p) {
    while (*p) if (*s++ != *p++) return 0;
    return 1;
}

/* ---------- Экран (VGA текстовый режим 80x25) ---------- */

#define COLS 80
#define ROWS 25
static volatile u16 *const vga = (u16 *)0xB8000;
static int cur_x, cur_y;
static u8 color = 0x0F;              /* белый на чёрном */

static void move_cursor(void) {
    u16 pos = cur_y * COLS + cur_x;
    outb(0x3D4, 14); outb(0x3D5, pos >> 8);
    outb(0x3D4, 15); outb(0x3D5, pos & 0xFF);
}

static void show_cursor(int on) {
    outb(0x3D4, 0x0A);
    outb(0x3D5, on ? 0x0E : 0x20);
}

static void put_at(int x, int y, char c, u8 col) { vga[y * COLS + x] = (u16)col << 8 | (u8)c; }

static void clear_screen(void) {
    for (int i = 0; i < COLS * ROWS; i++) vga[i] = (u16)color << 8 | ' ';
    cur_x = cur_y = 0;
    move_cursor();
}

static void scroll(void) {
    for (int i = 0; i < COLS * (ROWS - 1); i++) vga[i] = vga[i + COLS];
    for (int i = COLS * (ROWS - 1); i < COLS * ROWS; i++) vga[i] = (u16)color << 8 | ' ';
    cur_y = ROWS - 1;
}

static void putc(char c) {
    if (c == '\n') { cur_x = 0; cur_y++; }
    else if (c == '\b') {
        if (cur_x > 0) cur_x--;
        put_at(cur_x, cur_y, ' ', color);
    } else {
        put_at(cur_x, cur_y, c, color);
        if (++cur_x == COLS) { cur_x = 0; cur_y++; }
    }
    if (cur_y == ROWS) scroll();
    move_cursor();
}

static void print(const char *s) { while (*s) putc(*s++); }

static void print_num(int n) {
    char buf[12]; int i = 0;
    if (n < 0) { putc('-'); n = -n; }
    do { buf[i++] = '0' + n % 10; n /= 10; } while (n);
    while (i) putc(buf[--i]);
}

static void print_2d(int n) { putc('0' + n / 10); putc('0' + n % 10); }

/* ---------- Таймер (микросхема PIT, 1000 тиков в секунду) ---------- */

static u32 ticks;
static u16 pit_last;

static void timer_init(void) {
    outb(0x43, 0x34);                /* канал 0, режим 2 */
    outb(0x40, 1193 & 0xFF);         /* 1193182 Гц / 1193 ≈ 1000 Гц */
    outb(0x40, 1193 >> 8);
    pit_last = 1193;
}

/* Прерывания мы не используем, поэтому время узнаём, опрашивая счётчик. */
static void timer_poll(void) {
    outb(0x43, 0x00);
    u16 now = inb(0x40);
    now |= (u16)inb(0x40) << 8;
    if (now > pit_last) ticks++;     /* счётчик перезапустился — прошла 1 мс */
    pit_last = now;
}

/* ---------- Клавиатура (PS/2) ---------- */

enum { KEY_UP = 1, KEY_DOWN, KEY_LEFT, KEY_RIGHT, KEY_ESC = 27 };

static const char keymap[128] = {
    0, KEY_ESC, '1','2','3','4','5','6','7','8','9','0','-','=','\b',
    '\t','q','w','e','r','t','y','u','i','o','p','[',']','\n',
    0, 'a','s','d','f','g','h','j','k','l',';','\'','`',
    0, '\\','z','x','c','v','b','n','m',',','.','/', 0,
    '*', 0, ' ', [0x4A] = '-', [0x4E] = '+',
};
static const char keymap_shift[128] = {
    0, KEY_ESC, '!','@','#','$','%','^','&','*','(',')','_','+','\b',
    '\t','Q','W','E','R','T','Y','U','I','O','P','{','}','\n',
    0, 'A','S','D','F','G','H','J','K','L',':','"','~',
    0, '|','Z','X','C','V','B','N','M','<','>','?', 0,
    '*', 0, ' ', [0x4A] = '-', [0x4E] = '+',
};

static int shift, extended;

/* Возвращает код нажатой клавиши или 0, если ничего не нажато. */
static int key_poll(void) {
    timer_poll();
    if (!(inb(0x64) & 1)) return 0;
    u8 sc = inb(0x60);
    if (sc == 0xE0) { extended = 1; return 0; }
    if (sc == 0x2A || sc == 0x36) { shift = 1; return 0; }
    if (sc == 0xAA || sc == 0xB6) { shift = 0; return 0; }
    if (sc & 0x80) { extended = 0; return 0; }   /* отпускание клавиши */
    if (extended) {
        extended = 0;
        switch (sc) {
            case 0x48: return KEY_UP;
            case 0x50: return KEY_DOWN;
            case 0x4B: return KEY_LEFT;
            case 0x4D: return KEY_RIGHT;
        }
        return 0;
    }
    return shift ? keymap_shift[sc] : keymap[sc];
}

static int key_wait(void) {
    int k;
    while (!(k = key_poll())) ;
    return k;
}

static void read_line(char *buf, int max) {
    int n = 0;
    for (;;) {
        int k = key_wait();
        if (k == '\n') { buf[n] = 0; putc('\n'); return; }
        if (k == '\b') { if (n > 0) { n--; putc('\b'); } }
        else if (k >= ' ' && k < 127 && n < max - 1) { buf[n++] = k; putc(k); }
    }
}

/* ---------- Часы реального времени (CMOS) ---------- */

static u8 cmos(u8 reg) { outb(0x70, reg); return inb(0x71); }
static int bcd(u8 v) { return (v >> 4) * 10 + (v & 0xF); }

static void cmd_time(void) {
    while (cmos(0x0A) & 0x80) ;      /* ждём, пока часы не обновляются */
    int s = cmos(0x00), m = cmos(0x02), h = cmos(0x04);
    int d = cmos(0x07), mo = cmos(0x08), y = cmos(0x09);
    if (!(cmos(0x0B) & 0x04)) { s = bcd(s); m = bcd(m); h = bcd(h); d = bcd(d); mo = bcd(mo); y = bcd(y); }
    print("Date: 20"); print_2d(y); putc('-'); print_2d(mo); putc('-'); print_2d(d);
    print("  Time (UTC): "); print_2d(h); putc(':'); print_2d(m); putc(':'); print_2d(s); putc('\n');
}

/* ---------- Калькулятор: calc 2+3*4 ---------- */

static const char *expr_p;
static int expr_err;

static void skip_spaces(void) { while (*expr_p == ' ') expr_p++; }
static int parse_sum(void);

static int parse_atom(void) {
    skip_spaces();
    if (*expr_p == '(') { expr_p++; int v = parse_sum(); skip_spaces(); if (*expr_p == ')') expr_p++; else expr_err = 1; return v; }
    if (*expr_p == '-') { expr_p++; return -parse_atom(); }
    if (*expr_p < '0' || *expr_p > '9') { expr_err = 1; return 0; }
    int v = 0;
    while (*expr_p >= '0' && *expr_p <= '9') v = v * 10 + (*expr_p++ - '0');
    return v;
}

static int parse_product(void) {
    int v = parse_atom();
    for (;;) {
        skip_spaces();
        char op = *expr_p;
        if (op != '*' && op != '/' && op != '%') return v;
        expr_p++;
        int r = parse_atom();
        if (op != '*' && r == 0) { expr_err = 2; return 0; }
        v = op == '*' ? v * r : op == '/' ? v / r : v % r;
    }
}

static int parse_sum(void) {
    int v = parse_product();
    for (;;) {
        skip_spaces();
        char op = *expr_p;
        if (op != '+' && op != '-') return v;
        expr_p++;
        int r = parse_product();
        v = op == '+' ? v + r : v - r;
    }
}

static void cmd_calc(const char *e) {
    expr_p = e; expr_err = 0;
    int v = parse_sum();
    skip_spaces();
    if (*expr_p) expr_err = 1;
    if (expr_err == 2) print("Error: division by zero\n");
    else if (expr_err) print("Usage: calc 2+3*(4-1)\n");
    else { print_num(v); putc('\n'); }
}

/* ---------- Игра «Змейка» ---------- */

static u32 rng = 12345;
static u32 rand(void) { rng = rng * 1103515245 + 12345; return rng >> 16; }

#define TOP 1                        /* строка 0 — для счёта */
static u8 sx[COLS * ROWS], sy[COLS * ROWS];

static int snake_cell_busy(int x, int y, int len) {
    for (int i = 0; i < len; i++) if (sx[i] == x && sy[i] == y) return 1;
    return 0;
}

static void snake_status(int score, int best) {
    for (int x = 0; x < COLS; x++) put_at(x, 0, ' ', 0x1F);
    const char *t = " SNAKE  |  arrows/WASD - move  |  ESC - exit  |  score: ";
    int x = 0;
    while (*t) put_at(x++, 0, *t++, 0x1F);
    char buf[24]; int n = 0, v = score;
    do { buf[n++] = '0' + v % 10; v /= 10; } while (v);
    while (n) put_at(x++, 0, buf[--n], 0x1E);
    t = "  best: ";
    while (*t) put_at(x++, 0, *t++, 0x1F);
    v = best;
    do { buf[n++] = '0' + v % 10; v /= 10; } while (v);
    while (n) put_at(x++, 0, buf[--n], 0x1E);
}

static int snake_best;

static void snake(void) {
    show_cursor(0);
    rng ^= ticks;
restart:
    for (int i = 0; i < COLS * ROWS; i++) vga[i] = 0x0000;
    int len = 4, dx = 1, dy = 0, score = 0;
    for (int i = 0; i < len; i++) { sx[i] = 20 - i; sy[i] = 12; }
    int fx, fy;
    do { fx = rand() % COLS; fy = TOP + rand() % (ROWS - TOP); } while (snake_cell_busy(fx, fy, len));
    put_at(fx, fy, '@', 0x0C);
    for (int i = 0; i < len; i++) put_at(sx[i], sy[i], i ? 'o' : 'O', 0x0A);
    snake_status(score, snake_best);

    for (;;) {
        /* Ждём один «шаг», собирая нажатия клавиш. */
        u32 end = ticks + (score < 20 ? 110 - score * 3 : 50);
        int ndx = dx, ndy = dy;
        while (ticks < end) {
            int k = key_poll();
            if (k == KEY_ESC) goto quit;
            if ((k == KEY_UP || k == 'w') && dy == 0)    { ndx = 0; ndy = -1; }
            if ((k == KEY_DOWN || k == 's') && dy == 0)  { ndx = 0; ndy = 1; }
            if ((k == KEY_LEFT || k == 'a') && dx == 0)  { ndx = -1; ndy = 0; }
            if ((k == KEY_RIGHT || k == 'd') && dx == 0) { ndx = 1; ndy = 0; }
        }
        dx = ndx; dy = ndy;

        int nx = sx[0] + dx, ny = sy[0] + dy;
        if (nx < 0) nx = COLS - 1;
        if (nx >= COLS) nx = 0;
        if (ny < TOP) ny = ROWS - 1;
        if (ny >= ROWS) ny = TOP;

        int ate = nx == fx && ny == fy;
        if (snake_cell_busy(nx, ny, ate ? len : len - 1)) break;   /* врезались в себя */

        if (!ate) put_at(sx[len - 1], sy[len - 1], ' ', 0x00);
        else len++;
        for (int i = len - 1; i > 0; i--) { sx[i] = sx[i - 1]; sy[i] = sy[i - 1]; }
        sx[0] = nx; sy[0] = ny;
        put_at(sx[1], sy[1], 'o', 0x0A);
        put_at(nx, ny, 'O', 0x0A);

        if (ate) {
            score++;
            if (score > snake_best) snake_best = score;
            if (len >= COLS * (ROWS - TOP)) break;
            do { fx = rand() % COLS; fy = TOP + rand() % (ROWS - TOP); } while (snake_cell_busy(fx, fy, len));
            put_at(fx, fy, '@', 0x0C);
            snake_status(score, snake_best);
        }
    }

    {
        const char *msg = "  GAME OVER!  ENTER - play again, ESC - exit  ";
        int x = (COLS - str_len(msg)) / 2;
        for (int i = 0; msg[i]; i++) put_at(x + i, ROWS / 2, msg[i], 0x4F);
        for (;;) {
            int k = key_wait();
            if (k == '\n') goto restart;
            if (k == KEY_ESC) break;
        }
    }
quit:
    show_cursor(1);
    clear_screen();
}

/* ---------- Командная строка ---------- */

static void banner(void) {
    color = 0x0B;
    print("  _   _                    ___  ____\n");
    print(" | \\ | | _____   ____ _   / _ \\/ ___|\n");
    print(" |  \\| |/ _ \\ \\ / / _` | | | | \\___ \\\n");
    print(" | |\\  | (_) \\ V / (_| | | |_| |___) |\n");
    print(" |_| \\_|\\___/ \\_/ \\__,_|  \\___/|____/\n\n");
    color = 0x0F;
    print("Welcome to NovaOS 0.1! Type 'help' to see commands.\n\n");
}

static void cmd_help(void) {
    print("Commands:\n");
    print("  help        - this list\n");
    print("  clear       - clear the screen\n");
    print("  echo TEXT   - print TEXT\n");
    print("  calc EXPR   - calculator, e.g. calc (2+3)*4\n");
    print("  time        - current date and time\n");
    print("  uptime      - how long the system is running\n");
    print("  mem         - how much memory the computer has\n");
    print("  color N     - text color 1..15\n");
    print("  snake       - play Snake!\n");
    print("  about       - about NovaOS\n");
    print("  reboot      - restart the computer\n");
    print("  halt        - stop the computer\n");
}

static u32 mem_kb;

static void run_command(char *line) {
    while (*line == ' ') line++;
    if (!*line) return;

    if (str_eq(line, "help")) cmd_help();
    else if (str_eq(line, "clear")) clear_screen();
    else if (str_eq(line, "echo")) putc('\n');
    else if (starts_with(line, "echo ")) { print(line + 5); putc('\n'); }
    else if (starts_with(line, "calc ")) cmd_calc(line + 5);
    else if (str_eq(line, "time")) cmd_time();
    else if (str_eq(line, "uptime")) {
        u32 s = ticks / 1000;
        print("Up for "); print_num(s / 60); print(" min "); print_num(s % 60); print(" sec\n");
    }
    else if (str_eq(line, "mem")) {
        if (mem_kb) { print("Memory: "); print_num(mem_kb / 1024); print(" MB\n"); }
        else print("Memory size unknown\n");
    }
    else if (starts_with(line, "color ")) {
        int c = 0; const char *p = line + 6;
        while (*p >= '0' && *p <= '9') c = c * 10 + (*p++ - '0');
        if (c >= 1 && c <= 15) color = (u8)c; else print("Usage: color 1..15\n");
    }
    else if (str_eq(line, "snake")) snake();
    else if (str_eq(line, "about")) {
        print("NovaOS 0.1 - a tiny hobby operating system.\n");
        print("Written from scratch in C and assembly. No Linux, no Windows inside.\n");
    }
    else if (str_eq(line, "reboot")) { outb(0x64, 0xFE); }
    else if (str_eq(line, "halt")) {
        print("It is now safe to turn off your computer.\n");
        for (;;) __asm__ volatile("cli; hlt");
    }
    else { print("Unknown command: "); print(line); print("  (try 'help')\n"); }
}

struct multiboot_info { u32 flags, mem_lower, mem_upper; };

void kernel_main(u32 magic, struct multiboot_info *mb) {
    if (magic == 0x2BADB002 && (mb->flags & 1)) mem_kb = mb->mem_lower + mb->mem_upper + 1024;
    timer_init();
    clear_screen();
    banner();

    char line[128];
    for (;;) {
        u8 old = color;
        color = 0x0A; print("nova");
        color = 0x0F; print("> ");
        color = old;
        read_line(line, sizeof line);
        run_command(line);
    }
}
