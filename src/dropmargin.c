#include <u.h>
#include <libc.h>
#include <bio.h>

// rc(1) cmp(1) mk(1) /sys/src/cmd/col.c

int *levstop;
int levels;

int
marginstop(int readspaces){
	int i, lvls;

	lvls = levels;
	if(levstop == nil){
		levstop = (int*)calloc(levels, sizeof(int));
	}
	for(i = 0; i < levels; ++i) {
		if (levstop[i] == 0) {
			levstop[i] = readspaces;
			return readspaces;
		} else if (levstop[i] > readspaces) {
			while(lvls > i){			/* move known stops forward */
				levstop[--lvls] = levstop[lvls - 1];
			}
			levstop[i] = readspaces;	/* introduce the new stop */
			return readspaces;
		} else if (levstop[i] == readspaces) {
			while(++i < lvls){
				levstop[i] = 0;		/* reset next levels */
			}
			return readspaces;
		}
	}
	return levstop[levels - 1];
}

static void
usage(char *error)
{
	fprint(2, "usage: %s [-t tabstop] [-l levels]\n", argv0);
	if(error != nil)
		fprint(2, "invalid argument: %s\n", error);
	exits("usage");
}

void
main(int argc, char **argv){
	Biobuf bin, bout;
	char *f;
	long ch;
	Rune c;
	int tabstop, rspc;

	levels = 1;
	if(f = getenv("tabstop")){
		tabstop = atoi(f);
		free(f);
	} else {
		tabstop = 8;
	}

	ARGBEGIN{
	case 't':
		f=ARGF();
		tabstop=atoi(f);
		if(tabstop < 2 || tabstop & (tabstop -  1))
			usage("tabstop must be a power of 2.");
		break;
	case 'l':
		f=ARGF();
		levels=atoi(f);
		if(levels < 1)
			usage("levels must be greater than 1.");
		break;
	default:
		usage(nil);
	}ARGEND;


	rspc = 0;

	Binit(&bin, 0, OREAD);
	Binit(&bout, 1, OWRITE);

	while((ch = Bgetrune(&bin)) != Beof){
		c = ch;
		switch(c) {
		case '\0':
			break;
		case '\n':
			rspc=0;
			Bputc(&bout, ch);
			Bflush(&bout);
			break;
		case '\t':
			if(rspc > -1) {
				rspc += tabstop - (rspc % tabstop);
			} else {
				Bputc(&bout, '\t');
			}
			break;
		case ' ':
			if(rspc > -1) {
				++rspc;
			} else {
				Bputc(&bout, ' ');
			}
			break;
		default:
			if(rspc > -1) {
				rspc -= marginstop(rspc);
				while(rspc >= tabstop){
					Bputc(&bout, '\t');
					rspc -= tabstop;
				}
				while(rspc-- > 0)
					Bputc(&bout, ' ');
				rspc = -1;
			}
			Bputrune(&bout, ch);
			if(f = Brdstr(&bin, '\n', 0)){
				Bwrite(&bout, f, Blinelen(&bin));
				free(f);
				rspc = 0;
				Bflush(&bout);
			}
			break;
		}
	}

	//print("Hello world\n");
	exits(nil);
}