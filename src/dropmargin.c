#include <u.h>
#include <libc.h>
#include <bio.h>

// Edit ,s/margins/margins/g

#define USAGE "usage: %s [-t tabstop] [-l levels] [-n]\n"
typedef struct Line Line;
struct Line
{
	int level;
	int initialspaces;
	int table;
	int len;
	char *raw;
	char *content;
};


int levels;
int addnl;
int tabstop;

int *margins;
Line *prev; /* previous line read */

static void
usage(char *error) {
	if(error != nil)
		fprint(2, "invalid argument: %s\n", error);
	fprint(2, USAGE, argv0);
	exits("usage");
}

int
margin(Line *l) {
	if (l->initialspaces >= margins[l->level])
		return margins[l->level];
	return l->initialspaces;
}

void
updatemargin(int level, int spaces) {
	margins[level] = spaces;
	if(!addnl)
		margins[level] -= level;
}

int
getlevel(int initialspaces) {
	int i, l;
	
	for(i = 0; i < levels; ++i) {
		if(margins[i] == 0) {
			updatemargin(i, initialspaces);
			return i;
		}else if (margins[i] > initialspaces) {
			/* move known stops forward */
			for(l = levels - 1; l > i; --l)
				margins[l] = margins[l-1];
			/* introduce the new stop */
			updatemargin(i, initialspaces);
			return i;
		}else if (margins[i] == initialspaces) {
			/* reset next levels */
			for(l = levels - 1; l > i; --l)
				margins[l] = 0;
			return i;
		}
	}
	return levels - 1;
}

Line *
readLine(Biobufhdr *bp) {
	Line *l;
	char *c;
	
	l = (Line *)calloc(1, sizeof(Line));
	l->level = levels;
	if(c = Brdstr(bp, '\n', 0))
	{
		l->raw = c;
		l->len = Blinelen(bp);
			
		while (*c && !l->table) {
			switch(*c) {
			case '\n':
				l->level=0;
				break;
			case ' ':
				if (!l->content)
					l->initialspaces++;
				else 
					l->table = *(c-1) == ' ';
				break;
			case '\t':
				if (!l->content)
					l->initialspaces+=tabstop;
				else 
					l->table = 1;
				break;
			default:
				if (!l->content) {
					l->content = c;
					l->level = getlevel(l->initialspaces);
					l->initialspaces -= margin(l);
					l->len -= l->content - l->raw;
				}
				break;
			}
			++c;
		}
		return l;
	}
	return nil;
}

void 
freeLine(Line * ln) {
	free(ln->raw);
	free(ln);
}

void
main(int argc, char **argv) {
	Biobuf bin, bout;
	char *f;
	Line *l;
	int m;

	levels = 1;
	addnl = 0;
	
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
		if(tabstop < 2)
			usage("tabstop must be a greater than 1.");
		break;
	case 'l':
		f=ARGF();
		levels=atoi(f);
		if(levels < 1)
			usage("levels must be greater than 1.");
		break;
	case 'n':
		addnl = 1;
		break;
	default:
		usage(nil);
	}ARGEND;

	margins = (int*)calloc(1, levels * sizeof(int));

	Binit(&bin, 0, OREAD);
	Binit(&bout, 1, OWRITE);

	while(l = readLine(&bin)) {
		m = margin(l);
		Bwrite(&bout, l->content + m, l->len - m);
		Bflush(&bout);
		if(prev)
			free(prev);
		prev = l;
	}
}

int
marginstop(int readspaces) {
	int i, lvls;

	lvls = levels;
	if(margins == nil){
		margins = (int*)calloc(levels, sizeof(int));
	}
	for(i = 0; i < levels; ++i) {
		if (!addnl && i > 0)
			/* leave a one-space margin to levels > 0 */
			--readspaces;	
		if (margins[i] == 0) {
			margins[i] = readspaces;
			return readspaces;
		} else if (margins[i] > readspaces) {
			/* move known stops forward */
			while(lvls > i){
				margins[--lvls] = margins[lvls - 1];
			}
			/* introduce the new stop */
			margins[i] = readspaces;
			return readspaces;
		} else if (margins[i] == readspaces) {
			/* reset next levels */
			while(++i < lvls){
				margins[i] = 0;
			}
			return readspaces;
		}
	}
	return margins[levels - 1];
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
/*
void
main(int argc, char **argv) {
	Biobuf bin, bout;
	char *f;
	long ch;
	Rune c;
	int rspc, margin, lastm;

	levels = 1;
	addnl = 0;
	
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
		if(tabstop < 2)
			usage("tabstop must be a greater than 1.");
		break;
	case 'l':
		f=ARGF();
		levels=atoi(f);
		if(levels < 1)
			usage("levels must be greater than 1.");
		break;
	case 'n':
		addnl = 1;
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
				if (addnl && lastm && margin > lastm)
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
	*/
