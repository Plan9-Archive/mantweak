#include <u.h>
#include <libc.h>
#include <bio.h>

#define USAGE "usage: %s [-t tabstop] [-l levels] [-n]\n"

int *levstop;
int levels;
int newlines;
int tabstop;

int
marginstop(int readspaces) {
	int i, lvls;

	lvls = levels;
	if(levstop == nil){
		levstop = (int*)calloc(levels, sizeof(int));
	}
	for(i = 0; i < levels; ++i) {
		if (!newlines && i > 0)
			/* leave a one-space margin to levels > 0 */
			--readspaces;	
		if (levstop[i] == 0) {
			levstop[i] = readspaces;
			return readspaces;
		} else if (levstop[i] > readspaces) {
			/* move known stops forward */
			while(lvls > i){
				levstop[--lvls] = levstop[lvls - 1];
			}
			/* introduce the new stop */
			levstop[i] = readspaces;
			return readspaces;
		} else if (levstop[i] == readspaces) {
			/* reset next levels */
			while(++i < lvls){
				levstop[i] = 0;
			}
			return readspaces;
		}
	}
	return levstop[levels - 1];
}

static void
usage(char *error) {
	fprint(2, USAGE, argv0);
	if(error != nil)
		fprint(2, "invalid argument: %s\n", error);
	exits("usage");
}

void
printspaces(Biobuf *bout, int spaces) {
	while (spaces >= tabstop) {
		Bputc(bout, '\t');
		spaces -= tabstop;
	}
	while(spaces-- > 0)
		Bputc(bout, ' ');
}

void
main(int argc, char **argv) {
	Biobuf bin, bout;
	char *f;
	long ch;
	Rune c;
	int rspc, margin, lastm;

	levels = 1;
	newlines = 0;
	
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
	case 'n':
		newlines = 1;
		break;
	default:
		usage(nil);
	}ARGEND;


	rspc = 0;
	lastm = 0;

	Binit(&bin, 0, OREAD);
	Binit(&bout, 1, OWRITE);

	while((ch = Bgetrune(&bin)) != Beof){
		c = ch;
		switch(c) {
		case '\n':
			rspc=0;
			lastm = 0;
			Bputc(&bout, ch);
			Bflush(&bout);
			break;
		case '\t':
			if (rspc > -1)
				rspc += tabstop - (rspc % tabstop);
			else
				Bputc(&bout, c);
			break;
		case ' ':
			if(rspc > -1)
				++rspc;
			else
				Bputc(&bout, c);
			break;
		default:
			if (rspc > -1) {
				margin = marginstop(rspc);
				if (newlines && lastm && margin > lastm)
					Bputc(&bout, '\n');
				printspaces(&bout, rspc - margin);
				lastm = margin;
				rspc = -1;
			}
			Bputrune(&bout, ch);
			if(f = Brdstr(&bin, '\n', 0)){
				Bwrite(&bout, f, Blinelen(&bin));
				Bflush(&bout);
				free(f);
				rspc = 0;
			}
			break;
		}
	}

	exits(nil);
}