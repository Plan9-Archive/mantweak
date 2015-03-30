#include <u.h>
#include <libc.h>
#include <ctype.h>
#include <bio.h>
#include <draw.h>

// openfont(2) runestringnwidth(2) utflen(2)
#define USAGE "usage: %s [-t tabstop] [-l levels] [-n] [-f font]\n"

/* the following variables are set only by parseArguments */
int levels;
int addnl;	/* -n */
int tabstop;
Font *font;	/* $font of -f font, but nil if -f does not have an path */
int zerowidth;	/* if font!=nil, size in pixel of one '0' */
int tabwidth;	/* if font!=nil, zerowidth * tabstop */
int spcwidth;	/* if font!=nil, size in pixel of one ASCII space */

enum LineType 
{
	Empty,			/* only spaces, tabs or newline */
	ParagraphLine,
	SectionTitle,
	TableLine
};

typedef struct Line Line;
struct Line
{
	enum LineType type;
	int initialspaces;
	int margin;
	int len;			/* length in chars */
	char *raw;
	char *content;
};

typedef void (*LineHandler)(Biobufhdr *bp, Line *);

typedef struct Column Column;
struct Column
{
	int size;		/* number of Runes */
	int width;	/* pixels */
	Column *next;
};
typedef struct Row Row;
struct Row
{
	Line *line;
	Row *next;
};
typedef struct Table Table;
struct Table
{
	int nrows;		/* rows count */
	Row *rows;		/* linked list of rows */
	Column *columns;	/* linked list of detected columns */
	int maxutflen;		/* max utflen(2) of lines (see detectColumns) */
};

int *margins;		/* spaces for each level of margin */
Line *prev; 		/* previous line read */
Table *table;		/* current table (if any) */

/* ctype.h unicode missing http://www.unicode.org/charts/PDF/U2000.pdf
 */
int
isunicodedash(Rune r){
	return r >= 0x2010 && r <= 0x2015;
}
int
isunicodebullet(Rune r){
	return (r >= 0x2020 && r <= 0x2024) || r == 0x25d8 || r == 0x25e6;
}

void
debugLine(Line *l, char *topic)
{
	fprint(2, "\t%s\n", topic);
	fprint(2, "l->content		%s", l->content);
	fprint(2, "l->margin		%d\n", l->margin);
	fprint(2, "l->len			%d\n", l->len);
	fprint(2, "l->type		%d\n", l->type);
	fprint(2, "l->initialspaces	%d\n", l->initialspaces);
	for(int i = 0; i < levels; ++i)
		fprint(2, "margins[%d]		%d\n", i, margins[i]);
	fprint(2, "\n");
}

void 
freeLine(Line * ln) {
	free(ln->raw);
	free(ln);
}

void
setMarginSize(int level, int spaces) {
	margins[level] = spaces;
	if(!addnl)
		/*	by subtracting level to margins[level]
			we add a single space for each margin level when 
			we don't replace indentation with \n (addnl == 0)
		*/
		margins[level] -= level;
}

/*	updates margins[] and set l->margin according.
 */
void
setMargin(Line *l)
{
	int i, j;

	switch(l->type){
	case Empty:
		/* initialspaces are ignored by writeLine on Empty lines */
		return;
	case SectionTitle:
		if(margins[0] == 0 || l->initialspaces < margins[0]){
			margins[0] = l->initialspaces;
		}
	case TableLine:
		if(prev && prev->type == TableLine)
			l->margin = prev->margin;
	case ParagraphLine:
		i = 0;
		if(!l->margin && !l->initialspaces){
			while(i<levels)
				margins[i++] = 0;
			return;
		}
		while(!l->margin && i < levels){
			if(margins[i] == 0){
				setMarginSize(i, l->initialspaces);
				l->margin = margins[i];
			}else if(margins[i] > l->initialspaces){
				/* move known stops forward */
				for(j = levels - 1; j > i; --j)
					margins[j] = margins[j-1];
				/* introduce the new stop */
				setMarginSize(i, l->initialspaces);
				l->margin = margins[i];
			}else if (margins[i] == l->initialspaces){
				/* reset next levels */
				for(j = levels - 1; j > i; --j)
					margins[j] = 0;
				l->margin = margins[i];
			}else
				++i;
		}
		
		if(!l->margin)
			l->margin = margins[levels - 1];
		break;
	}
}

#define EXCLUDESTITLE(r) (islowerrune(r) || (r < Runeself && ispunct(r)))
#define SUGGESTTABLE(r, p, p2) (isspacerune(r) && isspacerune(p) && \
		p2 != '.' && \
		(p2 != '\'' || (prev && prev->type == TableLine)))

Line *
readLine(Biobufhdr *bp)
{
	Line *l;
	char *c;
	Rune r, p, p2;
	
	if (c = Brdstr(bp, '\n', 0)) {
		l = (Line *)malloc(sizeof(Line));
		l->raw = c;
		l->len = Blinelen(bp);
		l->type = Empty;
		l->content = nil;
		l->initialspaces = 0;
		l->margin = 0;
		
		while(*c && !l->content){
			switch(*c){
			case '\n':
				l->content = c;
				break;
			case ' ':
				l->initialspaces++;
				break;
			case '\t':
				l->initialspaces += tabstop - (l->initialspaces % tabstop);
				break;
			default:
				l->content = c;
				break;
			}
			if(!l->content)
				++c;
		}

		l->len -= l->content - l->raw;
		if(l->len > 1){
			/*	SectionTitles = lines without lowercase runes 
				TableLine =	lines with tabs or multiple 
							subsequent spaces 
				ParagraphLine = anything else
			 */
			r = 0; p = 0; p2 = 0;
			l->type = SectionTitle;
			while(*c && l->type != TableLine){
				c += chartorune(&r, c);
				if (r == '\t' || SUGGESTTABLE(r, p, p2))
					l->type = TableLine;
				else if(l->type == SectionTitle &&
						EXCLUDESTITLE(r))
					l->type = ParagraphLine;
				p2 = p;
				p = r;
			}
		}else
			l->initialspaces = 0;
	} else
		l = nil;
		
	return l;
}

void 
writeLine(Biobufhdr *bp, Line *l) {
	int i;
	i = l->initialspaces - l->margin;
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

int
runeWidth(Rune r){
	static int *cache;
	Rune i;
	
	assert(font != nil);
	
	if(cache == nil){
		cache = (int *)calloc(Runeself, sizeof(int));
		for(i = 32; i < Runeself; ++i)
			cache[i] = runestringnwidth(font, &i, 1);
	}
	if(r < Runeself)
		return cache[r];
	return runestringnwidth(font, &r, 1);
}

void
updateTable(Line *line){
	static Row *last;
	Row *newRow;
	int llen;

	assert(line->type == TableLine);

	newRow = (Row *)malloc(sizeof(Row));
	newRow->line = line;
	newRow->next = nil;
	if(table == nil){
		table = (Table *)malloc(sizeof(Table));
		table->nrows = 1;
		table->maxutflen = 0;
		table->columns = nil;
		table->rows = newRow;
	}else{
		last->next = newRow;
		++table->nrows;
	}
	last = newRow;
	llen = utflen(line->content);
	if(table->maxutflen < llen)
		table->maxutflen = llen;
}

void
debugIntArray(char *name, int len, int *arr){
	int i;
	fprint(2, "%s: [", name);
	for(i = 0; i<len; ++i){
		fprint(2, "%d,", arr[i]);
	}
	fprint(2, "]\n");
}


/* generally spaces are used to delimit table columns, but 
 * in man(6) we see that some punctuations (*) at column end
 * suppress the insertion of spaces. 
 */
#define ISDELIMC(x) (isspace(*(x)) || *(x) == '*')
#define ISDELIMR(x) (isspacerune(x))

Column *
detectColumns(Row *rows, int nrows, int maxutflen){
	int *nondelim;	/*	1 => not a column delimiter */
	int *runewidths;	/*	max pixel width of runes at i at each row */
	int *spccounts;	/*	max spaces before each index */
	Rune r;
	char *c;
	int i, j, w, consumed, s;
	Column *list, *last;
	
	if(nrows == 1){
		c = rows->line->content;
		chartorune(&r, c);
		if(!ispunct(*c) && !isdigit(*c) && 
			!isunicodedash(r) && !isunicodebullet(r))
			return nil;	/*  1 row => not a table */
	}

	nondelim = (int *)calloc(maxutflen, sizeof(int));
	runewidths = (int *)calloc(maxutflen, sizeof(int));
	spccounts = (int *)calloc(maxutflen, sizeof(int));
	while(rows){
		i = rows->line->initialspaces - rows->line->margin;
		s = i;
		while(s > 0)
			spccounts[s-1] = s--;
		c = rows->line->content;
		w = 0;
		while(*c && i < maxutflen){
			consumed = chartorune(&r, c);
			
			/* register rune width */
			if(r == '\t'){
				/* widths up to tabstop are at least 1em */
				for(j = i; j < maxutflen && j % tabstop > 0; ++j){
					w += zerowidth;
					if(runewidths[j] < w)
						runewidths[j] = w;
				}
			}else if(*c != '\n'){
				w += runeWidth(r);
				if(runewidths[i] < w)
					runewidths[i] = w;
			}
			
			/* count spaces */
			if(consumed == 1){
				if(*c == '\t'){
					/* we have a few spaces to skip */
					for(j = i; j % tabstop; ++j){
						if(spccounts[j] < ++s)
							spccounts[j] = s;
					}
				}else if(ISDELIMC(c)){
					if(spccounts[i] < ++s)
						spccounts[i] = s;
				}else
					s = 0;
			}else if(ISDELIMR(r)){
				if(spccounts[i] < ++s)
					spccounts[i] = s;
			}else
				s = 0;

			/* register if rune at i is not a delimiter */
			if(nondelim[i] == 0){
				if(consumed == 1){
					if(*c == '\t'){
						/* we have a few spaces to skip */
						i += tabstop - i % tabstop;
					}else if(*c == '*' && ISDELIMC(c+1)){
						/*	'*' are delimiters only
							before non delimiters (see man(6))
							NOTE: we should check the next 
							rune too, but this is good enough
						*/
						nondelim[i++] = 1;
					}else 
						nondelim[i++] = !ISDELIMC(c);
				}else 
					nondelim[i++] = !ISDELIMR(r);
			}else
				++i;		/* already found a word character in i */

			c += consumed;
		}
		rows = rows->next;
	}

	if(nrows < 3)	/* 1 - 2 rows => cannot detect one space delim */
		for(i = 0; i < maxutflen - 2; ++i)
			if(nondelim[i] && !nondelim[i+1] && nondelim[i+2])
				nondelim[i+1] = 1;
	
	list = (Column *)malloc(sizeof(Column));
	list->size = 0;
	list->width = 0;
	list->next = nil;
	last = list;
	w = 0;
	for(i = 0; i < maxutflen - 1; ++i){
		++last->size;
		if(nondelim[i])
			w = runewidths[i];
		if(!nondelim[i] && nondelim[i+1] && spccounts[i] > 1){// 
			/* a new column starts at i+1 */
			w -= runewidths[i - last->size];
			last->width = w - w % tabwidth;
			last->width += tabwidth;
			last->next = (Column *)malloc(sizeof(Column));
			last = last->next;
			last->size = 0;
			last->width = 0;
			last->next = nil;
		}
	}
	
	free(nondelim);
	free(runewidths);
	free(spccounts);

	if(last == list)
		return nil;	/* 1 column => not a table */

	return list;
}


void
debugTable(void){
	fprint(2, "table at:	%s",table->rows->line->content);
	fprint(2, "table->nrows = %d,	table->maxutflen = %d\n",table->nrows,table->maxutflen);

	Column *t = table->columns;
	int j = 0;
	while(t) {
		fprint(2, "column[%d]	size: %d,	width: %d\n", j++, t->size, t->width);
		t = t->next;
	}
}



void
writeTable(Biobufhdr *bp){
	Row *row;
	Column *col;
	int i, w, consumed, pendingspaces;
	char *c;
	Rune r;

	if(!table->columns)
		table->columns = detectColumns(table->rows, table->nrows, table->maxutflen);

	row = table->rows;
	if(!table->columns){
		/* not a table */
		while(row){
			row->line->type = ParagraphLine;
			writeLine(bp, row->line);
			row = row->next;
		}
		return;
	}else{
		while(row){
			col = table->columns;

			pendingspaces = row->line->initialspaces - row->line->margin;
			while(col && pendingspaces >= col->size){
				/* empty column at the beginning of line */
				for(w = 0; w < col->width; w += tabwidth)
					Bputc(bp, '\t');
				
				pendingspaces -= col->size;
				col = col->next;
			}

			pendingspaces = 0;
			i = 0;
			w = 0;
			c = row->line->content;
			while(*c && col){

				if(i == col->size && col->next){
					while(w < col->width){
						w += tabwidth - w % tabwidth;
						Bputc(bp, '\t');
					}
					i = 0;
					w = 0;
					pendingspaces = 0;
					col = col->next;
				}
				consumed = chartorune(&r, c);

				if(r == ' ')
					++pendingspaces;
				else{
					while(pendingspaces > 0){
						Bputc(bp, ' ');
						w += spcwidth;
						--pendingspaces;
					}
					Bputrune(bp, r);
					w += runeWidth(r);
				}


				c += consumed;
				++i;
			}
			row = row->next;
		}
	}
}

void
freeTable(void)
{
	void *p;
	Column *c;
	Row *r;
	
	c = table->columns;
	while(c){
		p = c;
		c = c->next;
		free(p);
	}
	r = table->rows;
	while(r){
		freeLine(r->line);
		p = r;
		r = r->next;
		free(p);
	}
	free(table);
}

static void
usage(char *error)
{
	if(error != nil)
		fprint(2, "invalid argument: %s\n", error);
	fprint(2, USAGE, argv0);
	exits("usage");
}

void
parseArguments(int argc, char **argv)
{
	char *s;

	levels = 1;
	addnl = 0;
	
	if(s = getenv("tabstop")){
		tabstop = atoi(s);
		free(s);
	} else {
		tabstop = 8;
	}

	s = getenv("font");
	if(s != nil){
		font = openfont(nil, s);
		free(s);
	} else
		font = nil;

	ARGBEGIN{
	case 't':
		s=ARGF();
		tabstop=atoi(s);
		if(tabstop < 2)
			usage("tabstop must be a greater than 1.");
		break;
	case 'l':
		s=ARGF();
		levels=atoi(s);
		if(levels < 1)
			usage("levels must be greater than 1.");
		break;
	case 'n':
		addnl = 1;
		break;
	case 'f':
		s = ARGF();
		if(font){
			/* an empty -f flag means no font adjustments */
			free(font);
			font = nil;
		}
		if(s){
			font = openfont(nil, s);
			if(!font)
				usage("can't open font file.");
		}
		break;
	default:
		usage(nil);
	}ARGEND;
	if(font){
		zerowidth = runeWidth('0');
		tabwidth = zerowidth * tabstop;
		spcwidth = runeWidth(' ');
	}
}

#define INDENTATIONCOLLAPSED(l,p) (p && l->type != Empty && \
		l->type != p->type && \
		l->initialspaces - l->margin == p->initialspaces - p->margin)

#define TABLECONTINUES(l,p) (p && p->type == TableLine && \
		l->type != Empty && \
		l->initialspaces >= prev->initialspaces)

void
handleLineOnFixedWidthFont(Biobufhdr *bp, Line *l){
	writeLine(bp, l);
	if(prev){
		freeLine(prev);
		prev = nil;
	}
	if(l->type == Empty)
		freeLine(l);
	else
		prev = l;	
}

void
handleLineOnVariableWidthFont(Biobufhdr *bp, Line *l){
	if(l->type == TableLine){
		updateTable(l);
	}else{
		if(table){
			writeTable(bp);
			freeTable();
			prev = nil;	/* prev has already been freed */
			table = nil;
		}
		writeLine(bp, l);
	}
	if(prev){
		if(l->type != TableLine)
			freeLine(prev);
		prev = nil;
	}
	if(l->type == Empty)
		freeLine(l);
	else
		prev = l;	
}

void
main(int argc, char **argv)
{
	Biobuf bin, bout;
	Line *l;
	LineHandler handleLine;

	parseArguments(argc, argv);

	/*	to keep code simple, use different logic
		with or without font */

	if(font)
		handleLine = handleLineOnVariableWidthFont;
	else
		handleLine = handleLineOnFixedWidthFont;

	margins = (int*)calloc(levels, sizeof(int));

	Binit(&bin, 0, OREAD);
	Binit(&bout, 1, OWRITE);

	while(l = readLine(&bin)) {
		if(TABLECONTINUES(l, prev))
			l->type = TableLine;
		setMargin(l);
		if(addnl && INDENTATIONCOLLAPSED(l, prev)) {
			Bputc(&bout, '\n');
		}
		handleLine(&bout, l);
	}
	if(font && table){
		/* a table is still waiting to be printed */
		writeTable(&bout);
	}
	exits(nil);
}