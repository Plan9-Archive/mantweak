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
	
	if (c = Brdstr(bp, '\n', 0)) {
		l = (Line *)malloc(sizeof(Line));
		l->raw = c;
		l->len = Blinelen(bp);
		l->level = 0;
		l->table = 0;
		l->content = nil;
		l->initialspaces = 0;
		
		while (*c && !l->table) {
			switch(*c) {
			case '\n':
				if (!l->content)
					l->content = c;
				break;
			case ' ':
				if (!l->content)
					l->initialspaces++;
				else 
					l->table = *(c-1) == ' ';
				break;
			case '\t':
				if (!l->content)
					l->initialspaces += tabstop;
				else 
					l->table = 1;
				break;
			default:
				if (!l->content) {
					l->content = c;
				}
				break;
			}
			++c;
		}
		l->len -= l->content - l->raw;
		if (l->len > 1){
			if (prev && prev->table) {
				l->level = prev->level;
				l->table = prev->table;
			} else {
				l->level = getlevel(l->initialspaces);
			}
			l->initialspaces -= margin(l);
		}
		return l;
	}
	return nil;
}

void 
writeLine(Biobufhdr *bp, Line *l) {
	int i;

	i = l->initialspaces;
	if (l->len) {
		while(i >= tabstop) {
			Bputc(bp, '\t');
			i -= tabstop;
		}
		while(i-- > 0)
			Bputc(bp, ' ');
		Bwrite(bp, l->content, l->len);
		Bflush(bp);
	}	
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

	margins = (int*)calloc(levels, sizeof(int));

	Binit(&bin, 0, OREAD);
	Binit(&bout, 1, OWRITE);

	while(l = readLine(&bin)) {
		if(prev && levels > 1) {
			if(!prev->level && !l->level && l->len > 1) {
			/*	section names hold one line, any successive line with the same
				margin is part of the section body.
				We change the first level of margin so that the following lines 
				will be correct
			*/
				//updatemargin(1, margins[0]);
				margins[1] = margins[0];
				margins[0] = 0;
				l->level++;
			}
			if(addnl && prev->level < l->level && !prev->table)
				Bputc(&bout, '\n');
		}
		writeLine(&bout, l);
		if(prev){
			freeLine(prev);
			prev = nil;
		}
		if(l->len > 1)
			prev = l;
		else
			freeLine(l);
	}

	exits(nil);
}
