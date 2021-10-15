#include <kprintf.h>
#include <screen.h>

static char buf[1024] = {-1};
static int ptr = -1;

static void
parse_num(unsigned int value, unsigned int base) {
	unsigned int n = value / base;
	int r = value % base;
	if (r < 0) {
		r += base;
		--n;
	}
	if (value >= base)
			parse_num(n, base);
	buf[ptr++] = "0123456789"[r];
}

/* change number to hex char
 * length 8
 */
static void
parse_hex(unsigned int value) {
	int i = 8;
	while (i-- > 0) {
		buf[ptr++] = "0123456789abcdef"[(value>>(i*4))&0xf];
	}
}
void abort(const char* fmt){
    kprintf("ABORT:%s\n",fmt);
    asm("hlt");
}

void kprintf(const char *fmt, ...) {
    int i = 0;
    char *s;
    enum KP_LEVEL kl=KPL_DUMP;
    /* must be the same size as enum KP_LEVEL */
    struct KPC_STRUCT {
            COLOUR fg;
            COLOUR bg;
    } KPL[] = {
            {BRIGHT_WHITE, BLACK},
            {YELLOW, RED},
    };
    args_list args;
    args_start(args, fmt);
    ptr=0;
    for (; fmt[i]; ++i) {
		if ((fmt[i]!='%') && (fmt[i]!='\\')) {
			buf[ptr++] = fmt[i];
			continue;
		} else if (fmt[i] == '\\') {
			/* \a \b \t \n \v \f \r \\ */
			switch (fmt[++i]) {
				case 'a': buf[ptr++] = '\a'; break;
				case 'b': buf[ptr++] = '\b'; break;
				case 't': buf[ptr++] = '\t'; break;
				case 'n': buf[ptr++] = '\n'; break;
				case 'r': buf[ptr++] = '\r'; break;
				case '\\':buf[ptr++] = '\\'; break;
			}
			continue;
		}
		/* fmt[i] == '%' */
		switch (fmt[++i]) {
			case 's':
				s = (char *)args_next(args, char *);
				while (*s)
					buf[ptr++] = *s++;
				break;
			case 'c':
				buf[ptr++] = (char)args_next(args, int);
				break;
			case 'x':
				parse_hex((unsigned long)args_next(args, unsigned long));
				break;
			case 'd':
				parse_num((unsigned long)args_next(args, unsigned long), 10);
				break;
			case '%':
				buf[ptr++] = '%';
				break;
			default:
				buf[ptr++] = fmt[i];
				break;
		}
    }
	buf[ptr] = '\0';
	args_end(args);
	for (i=0; i<ptr; ++i)
		print_c(buf[i], KPL[kl].fg, KPL[kl].bg);
}
void dumpmem(void *addr,uint32_t size){
    uint32_t i=0;
    uint32_t* ptr = addr;
    for(;i<size;i+=16){
        kprintf("%x: %x %x %x %x\n",(uint32_t)ptr,ptr[0],ptr[1],ptr[2],ptr[3]);
        ptr += 4;
    }
}






