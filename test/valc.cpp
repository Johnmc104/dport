#include "Vval.h"
#include "verilated.h"
#if VM_TRACE
#include "verilated_vcd_c.h"
#endif
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdarg.h>

Vval *top;

vluint64_t main_time = 0;
int twolane = 1;
int debug = 0;

uint8_t
scramble(uint8_t c, int isk)
{
	static uint16_t lfsr = 0xffff;
	int i;
	uint16_t n;

	n = lfsr >> 8;
	lfsr = lfsr << 8 ^ n ^ n << 3 ^ n << 4 ^ n << 5;
	n = n >> 4 | n << 4;
	n = n >> 2 & 0x33 | n << 2 & 0xcc;
	n = n >> 1 & 0x55 | n << 1 & 0xaa;
	if(isk){
		if(c == 0x1c){
			lfsr = 0xffff;
			return 0;
		}
		return 0;
	}
	return n;
}

double
sc_time_stamp()
{
	return main_time;
}

#define BS(c, isk) ((c) == 0xbc && (isk))
#define BE(c, isk) ((c) == 0xfb && (isk))
#define SS(c, isk) ((c) == 0x5c && (isk))
#define SE(c, isk) ((c) == 0xfd && (isk))
#define FS(c, isk) ((c) == 0xfe && (isk))
#define FE(c, isk) ((c) == 0xf7 && (isk))
#define SR(c, isk) ((c) == 0x1c && (isk))

int Mvid, Nvid, htotal, vtotal, hsw, hstart, vstart, vsw, hwidth, vheight, misc0, misc1, yctr, bctr;
double clk;
int fifo;

void
print(unsigned char c, int isk)
{
	static int row;

	if(isk)
		switch(c){
		case 0xbc: printf("BS "); break;
		case 0xfb: printf("BE "); break;
		case 0x5c: printf("SS "); break;
		case 0xfd: printf("SE "); break;
		case 0xfe: printf("FS "); break;
		case 0xf7: printf("FE "); break;
		case 0x1c: printf("SR "); break;
		default: printf("?(%x) ", c);
		}
	else
		printf("%.2x ", c);
	if(++row == 32){
		printf("\n");
		row = 0;
	}
}

void
note(char *fmt, ...)
{
	static char buf[256];
	va_list va;
	
	va_start(va, fmt);
	vsnprintf(buf, sizeof(buf), fmt, va);
	va_end(va);
	fprintf(stderr, "%s\n", buf);
}
#define error note

void
fatal(char *fmt, ...)
{
	static char buf[256];
	va_list va;
	
	va_start(va, fmt);
	vsnprintf(buf, sizeof(buf), fmt, va);
	va_end(va);
	fprintf(stderr, "%s\n", buf);
	exit(1);
}

void
parseattr(uint8_t *p, int n)
{
	if(twolane){
		Mvid = p[0] << 16 | p[2] << 8 | p[4];
		if(p[0] != p[1] || p[2] != p[3] || p[4] != p[5])
			error("second Mvid doesn't match first");
		htotal = p[6] << 8 | p[8];
		hstart = p[7] << 8 | p[9];
		vtotal = p[10] << 8 | p[12];
		vstart = p[11] << 8 | p[13];
		hsw = p[14] << 8 | p[16];
		vsw = p[15] << 8 | p[17];
		if((p[18] << 16 | p[20] << 8 | p[22]) != Mvid)
			error("third Mvid doesn't match first");
		if((p[19] << 16 | p[21] << 8 | p[23]) != Mvid)
			error("fourth Mvid doesn't match first");
		hwidth = p[24] << 8 | p[26];
		Nvid = p[25] << 16 | p[27] << 8 | p[29];
		vheight = p[28] << 8 | p[30];
		misc0 = p[31];
		misc1 = p[33];
		if(p[32] != 0 || p[34] != 0 || p[35] != 0)
			error("zeroes not zero");
	}else{
		Mvid = p[0] << 16 | p[1] << 8 | p[2];
		htotal = p[3] << 8 | p[4];
		vtotal = p[5] << 8 | p[6];
		hsw = p[7] << 8 | p[8];
		if((p[9] << 16 | p[10] << 8 | p[11]) != Mvid)
			error("second Mvid doesn't match first");
		hstart = p[12] << 8 | p[13];
		vstart = p[14] << 8 | p[15];
		vsw = p[16] << 8 | p[17];
		if((p[18] << 16 | p[19] << 8 | p[20]) != Mvid)
			error("third Mvid doesn't match first");
		hwidth = p[21] << 8 | p[22];
		vheight = p[23] << 8 | p[24];
		if(p[25] != 0 || p[26] != 0)
			error("1st zero word not zero");
		if((p[27] << 16 | p[28] << 8 | p[29]) != Mvid)
			error("third Mvid doesn't match first");
		Nvid = p[30] << 16 | p[31] << 8 | p[32];
		misc0 = p[33];
		misc1 = p[34];
		if(p[35] != 0)
			error("2nd zero not zero");
		if(n != 36)
			error("attribute packet wrong size %d != 36", n);
	}
	note("resolution %dx%d, total %dx%d, sync %d(%c)x%d(%c), start %dx%d, Mvid/Nvid %d/%d (%g)", hwidth, vheight, htotal, vtotal, hsw & 0x7fff, (hsw & 0x8000) ? '-' : '+', vsw & 0x7fff, (vsw & 0x8000) ? '-' : '+', hstart, vstart, Mvid, Nvid, (double)Mvid/Nvid*162);
	if(misc0 != 0x21 || misc1 != 0)
		note("unsupported mode");
}

void
hdata(uint8_t c, int reset)
{
	static uint32_t prng;
	static int n = 4;
	uint8_t d;
	
	if(reset){
		n = 4;
		prng = 0;
		return;
	}
	d = prng >> 8 * n;
	if(d != c)
		error("wrong byte %x != %x", c, d);
	if(++n == 3){
		prng = 1664525 * prng + 1013904223;
		n = 0;
	}
	if(n == 10)
		n = 0;
}

void
hdata2(uint8_t c, uint8_t e, int reset)
{
	static uint32_t prng, prng0;
	static int n = 4;
	uint8_t d, f;
	
	if(reset){
		n = 4;
		prng = 0;
		prng0 = 0;
		return;
	}
	d = prng >> 8 * n;
	f = prng0 >> 8 * n;
	if(d != c)
		error("wrong byte (l0) %x != %x", c, d);
	if(f != e)
		error("wrong byte (l1) %x != %x", e, f);
	if(++n == 3){
		prng = 1664525 * prng0 + 1013904223;
		prng0 = 1664525 * prng + 1013904223;
		n = 0;
	}
	if(n == 7){
		n = 0;
		prng0 = 1013904223;
	}
}

void
out(uint8_t c, int isk, uint8_t c1, int isk1)
{
	static int state;
	static uint8_t buf[64];
	static int n;
	static uint8_t c0;
	static int isk0;
	enum {
		START,
		ATTR,
		VBL,
		HDATA,
		HFILL,
		HEND,
		HBL
	};
	uint8_t s;

	s = scramble(c, isk);
	if(!isk)
		c ^= s;
	else if(c == 0x1c)
		c = 0xbc;
	if(!isk1)
		c1 ^= s;
	else if(c1 == 0x1c)
		c1 = 0xbc;
	if(debug){
		print(c, isk);
		if(twolane)
			print(c1, isk1);
	}
	if(isk != isk1 || isk1 && c != c1)
		error("lanes disagree %x != %x", c, c1);
	switch(state){
	case START:
		if(SS(c, isk) && SS(c0, isk0)){
			note("found SS/SS");
			state = ATTR;
		}
		break;
	case ATTR:
		if(n == sizeof(buf))
			fatal("attribute packet too long");
		if(isk)
			if(SE(c, isk)){
				parseattr(buf, n);
				n = 0;
				state = VBL;
			}else
				fatal("invalid K char %x in attribute packet", c);
		else{
			buf[n++] = c;
			if(twolane)
				buf[n++] = c1;
		}
		break;
	case VBL:
		if(BE(c, isk)){
			note("found BE");
			state = HDATA;
			yctr = 0;
			bctr = 0;
		}
		break;
	case HDATA:
		if(isk)
			if(FS(c, isk))
				state = HFILL;
			else if(BS(c, isk))
				state = HEND;
			else if(FE(c, isk))
				state = HDATA;
			else
				error("invalid K char %x in horizontal data", c);
		else{
			bctr++;
			bctr += twolane;
			fifo++;
			if(twolane)
				hdata2(c, c1, 0);
			else
				hdata(c, 0);
			if(fifo > 32)
				error("fifo overrun");
		}
		break;
	case HFILL:
		if(isk)
			if(FE(c, isk))
				state = HDATA;
			else
				fatal("invalid K char %x in horizontal fill", c);
		break;
	case HEND:
		if(twolane && c != c1)
			error("mismatch between lanes in BS sequence");
		if(isk)
			fatal("invalid K char %x in hblank header", c);
		else switch(n){
		case 0:
			buf[0] = c;
			if(bctr != hwidth * 3)
				fatal("number of bytes per line %d != %d", bctr, hwidth * 3);
			if(c != (++yctr == vheight))
				fatal("VB-ID byte %x != %x", c, ++yctr == vheight);
			break;
		case 1:
			buf[1] = c;
			if(c != (Mvid & 0xff))
				fatal("Mvid after BS %x != %x", c, Mvid & 0xff);
			break;
		case 2:
			buf[2] = c;
			break;
		default:
			if(buf[n % 3] != c)
				error("expected bytes following BS to repeat %x != %x", c, buf[(n+1 & 3) - 1]); 
			if(n == (twolane ? 5 : 11)){
				n = 0;
				bctr = 0;
				fifo = 0;
				if(yctr == vheight){
					hdata(0, 1);
					hdata2(0, 0, 1);
					note("completed frame");
					state = VBL;
				}else
					state = HBL;
				goto skipinc;
			}
		}
		n++;
	skipinc:
		break;
	case HBL:
		if(BE(c, isk))
			state = HDATA;
		break;
	}
	if(state == HDATA || state == HFILL){
		clk += (double) Mvid / Nvid;
		if(clk >= 1){
			clk--;
			fifo -= 3;
			if(0 && fifo < 0)
				printf("fifo underrun\n");
		}
	}
	c0 = c;
	isk0 = isk;
}

int
main(int argc, char **argv)
{
	int dat0 = 0, dat1 = 0, isk0 = 0, isk1 = 0;

	Verilated::commandArgs(argc, argv);
	top = new Vval;
	
#if VM_TRACE
	Verilated::traceEverOn(true);
	VerilatedVcdC *tfp = new VerilatedVcdC;
	top->trace(tfp, 99);
	tfp->open("obj_dir/sim.vcd");
#endif
	top->dpclk = 0;
	top->twolane = twolane;
	top->reset = 1;
	while(!Verilated::gotFinish()){
		if(main_time == 10)
			top->reset = 0;
		if((main_time & 1) == 1)
			if(twolane){
				out(dat1, isk1 & 1, top->txdat1, top->txisk1 & 1);
				out(dat1 >> 8, isk1 >> 1, top->txdat1 >> 8, top->txisk1 >> 1);
				dat1 = dat0;
				dat0 = top->txdat0;
				isk1 = isk0;
				isk0 = top->txisk0;
			}else{
				out(top->txdat0, top->txisk0 & 1, top->txdat0, top->txisk0 & 1);
				out(top->txdat0 >> 8, top->txisk0 >> 1, top->txdat0 >> 8, top->txisk0 >> 1);
			}
		top->dpclk = !top->dpclk;
		top->eval();
		main_time++;
#if VM_TRACE
		tfp->dump(main_time);
#endif
	}

#if VM_TRACE
	tfp->close();
#endif
	delete top;
	exit(0);
}
