// implementation of generic tools

#include "cube.h"

////////////////////////// rnd numbers ////////////////////////////////////////

#define N              (624)             
#define M              (397)                
#define K              (0x9908B0DFU)       
#define hiBit(u)       ((u) & 0x80000000U)  
#define loBit(u)       ((u) & 0x00000001U)  
#define loBits(u)      ((u) & 0x7FFFFFFFU)  
#define mixBits(u, v)  (hiBit(u)|loBits(v)) 

static uint state[N+1];     
static uint *next;          
static int left = -1;     

void seedMT(uint seed)
{
    register uint x = (seed | 1U) & 0xFFFFFFFFU, *s = state;
    register int j;
    for(left=0, *s++=x, j=N; --j; *s++ = (x*=69069U) & 0xFFFFFFFFU);
}

uint reloadMT(void)
{
    register uint *p0=state, *p2=state+2, *pM=state+M, s0, s1;
    register int j;
    if(left < -1) seedMT(time(NULL));
    left=N-1, next=state+1;
    for(s0=state[0], s1=state[1], j=N-M+1; --j; s0=s1, s1=*p2++) *p0++ = *pM++ ^ (mixBits(s0, s1) >> 1) ^ (loBit(s1) ? K : 0U);
    for(pM=state, j=M; --j; s0=s1, s1=*p2++) *p0++ = *pM++ ^ (mixBits(s0, s1) >> 1) ^ (loBit(s1) ? K : 0U);
    s1=state[0], *p0 = *pM ^ (mixBits(s0, s1) >> 1) ^ (loBit(s1) ? K : 0U);
    s1 ^= (s1 >> 11);
    s1 ^= (s1 <<  7) & 0x9D2C5680U;
    s1 ^= (s1 << 15) & 0xEFC60000U;
    return(s1 ^ (s1 >> 18));
}

uint randomMT(void)
{
    uint y;
    if(--left < 0) return(reloadMT());
    y  = *next++;
    y ^= (y >> 11);
    y ^= (y <<  7) & 0x9D2C5680U;
    y ^= (y << 15) & 0xEFC60000U;
    return(y ^ (y >> 18));
}

// below is added by vampi

static const char *b64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
int base64_read_bit(char *s, int &bit) {
	int ofs = (bit >> 3);
	char *c = s + ofs;
	char *p;
	while(*c && !(p = strchr((char *)b64, *c))) { c++; bit += 8; }
	if(!*c) return -1;
	int v = p - b64;
	int mask = 0x20 >> (bit&0x07);
	int val = v & mask;
	bit++;
	if((bit & 0x07) >= 6) bit += 8 - (bit & 0x07);
	return val != 0;
}

int base64_read_byte(char *s, int &bit) {
	int val = 0;
	for(int i = 0; i < 8; i++) {
		int c = base64_read_bit(s, bit);
		if(c < 0) return -1;
		if(c) val |= (0x80 >> i);
	}
	return val;
}

bool base64_strcmp(const char *s, const char *s64) {
	const char *c = s; int b = 0;
	int bit = 0;
	while((b = base64_read_byte((char *)s64, bit)) >= 0) {
		if(*c != b) return false;
		c++;
	}
	if(!*c && b>=0) return false;
	if(b < 0 && *c) return false;
	return true;
}

void bufferevent_print_error(short what, const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);

#define checkerr(s) { if(what & BEV_EVENT_##s) printf(" %s", #s); }
	checkerr(CONNECTED);
	checkerr(READING);
	checkerr(WRITING);
	checkerr(EOF);
	checkerr(ERROR);
	checkerr(TIMEOUT);
	printf(" errno=%d \"%s\"\n", errno, strerror(errno));
}

void evdns_print_error(int result, const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);

#define DNSERR(x) if(result == DNS_ERR_##x) printf(" DNS_ERR_" #x);
	DNSERR(NONE); DNSERR(FORMAT); DNSERR(SERVERFAILED);
	DNSERR(NOTEXIST); DNSERR(NOTIMPL); DNSERR(REFUSED);
	DNSERR(TRUNCATED); DNSERR(UNKNOWN); DNSERR(TIMEOUT);
	DNSERR(SHUTDOWN); DNSERR(CANCEL);
	printf(" errno=%d \"%s\"\n", errno, strerror(errno));
}

void bufferevent_write_vprintf(struct bufferevent *be, const char *fmt, va_list ap) {
	struct evbuffer *eb = evbuffer_new();
	if(!eb) return;
	evbuffer_add_vprintf(eb, fmt, ap);
	bufferevent_write_buffer(be, eb);
	evbuffer_free(eb);
}

void bufferevent_write_printf(struct bufferevent *be, const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	bufferevent_write_vprintf(be, fmt, ap);
	va_end(ap);
}

char *evbuffer_readln_nul(struct evbuffer *buffer, size_t *n_read_out, enum evbuffer_eol_style eol_style) {
	size_t len;
	char *result = evbuffer_readln(buffer, n_read_out, eol_style);
	if(result) return result;
	len = evbuffer_get_length(buffer);
	if(len == 0) return NULL;
	if(!(result = (char *)malloc(len+1))) return NULL;
	evbuffer_remove(buffer, result, len);
	result[len] = '\0';
	if(n_read_out) *n_read_out = len;
	return result;
}

