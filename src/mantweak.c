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
int zerowidth;	/* if font!=nil, size in pixel of one "0" */
int tabwidth;	/* if font!=nil, zerowidth * tabstop */
int spcwidth;	/* if font!=nil, size in pixel of one ASCII space */

enum LineType 
{
	Empty,			/* only spaces, tabs or newline */
	Text,
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

void
debugLine(Line *l, char *topic)
{
	fprint(2, "\t%s\n", topic);
	fprint(2, "l->content		%s", l->content);
	fprint(2, "l->margin		%d\n", l->margin);
	fprint(2, "l->len			%d\n", l->len);
	//fprint(2, "l->level			%d\n", l->level);
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

/*	updates margins[] and set l->mrgin according.
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
	case Text:
		i = 0;
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


Line *
readLine(Biobufhdr *bp)
{
	Line *l;
	char *c;
	Rune r, p;
	
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
				Text = anything else
			 */
			r = 0; p = 0;
			l->type = SectionTitle;
			while(*c && l->type != TableLine){
				c += chartorune(&r, c);
				if (r == '\t' || (isspacerune(r) && isspacerune(p)))
					l->type = TableLine;
				else if(l->type == SectionTitle && islowerrune(r))
					l->type = Text;
				p = r;
			}
		}else
			l->initialspaces = 0;
		//debugLine(l, "readLine");
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

	//debugLine(line, "updateTable");

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

#define ISDELIMC(x) (isspace(*(x)) || ispunct(*(x)))
#define ISDELIMR(x) (isspacerune(x))

Column *
detectColumns(Row *rows, int nrows, int maxutflen){
	int *nondelim;	/*	1 => not a column delimiter */
	int *runewidths;	/*	max pixel width of runes at i at each row */
	Rune r;
	char *c;
	int i, j, w, consumed;
	Column *list, *last;
	
	if(nrows == 1)
		return nil;	/*  1 row => not a table */

	nondelim = (int *)calloc(maxutflen, sizeof(int));
	runewidths = (int *)calloc(maxutflen, sizeof(int));
	while(rows){
		/* This implementation count initial spaces as 1em each
		i = 0;
		j = rows->line->initialspaces - rows->line->margin;
		while(i < j){
			if(runewidths[i] < zerowidth)
				runewidths[i] = zerowidth;
			++i;
		}
		* but a simpler one just skip such spaces counting them
		* as 0px. In some cases this could reduce the first column
		* width a bit.
		* Thus set i to index of the first non space character:
		*/
		i = rows->line->initialspaces - rows->line->margin;
		c = rows->line->content;
//		fprint(2, "NEWLINE\n");
		while(*c){
			consumed = chartorune(&r, c);
//			fprint(2, "found %C: ", r);
			
			/* register rune width */
			if(r == '\t'){
				/* widths up to tabstop are at least 1em */
				for(j = i; j % tabstop > 0; ++j)
					if(runewidths[j] < zerowidth)
						runewidths[j] = zerowidth;
			}else if(*c != '\n'){
				w = runeWidth(r);
				if(runewidths[i] < w)
					runewidths[i] = w;
			}
			/* register if rune at i is not a delimiter */
			if(nondelim[i] == 0){
				if(consumed == 1){
					if(*c == '\t')
						/* we have a few spaces to skip */
						i += tabstop - i % tabstop;
					else if(ispunct(*c) && ISDELIMC(c+1))
						/*	punctuations are delimiters only
							before non delimiters
							NOTE: we should check the next 
							rune too, but this is good enough
						*/
						nondelim[i++] = 1;
					else
						nondelim[i++] = !ISDELIMC(c);
				}else
					nondelim[i++] = !ISDELIMR(r);
			}else
				++i;		/* already found a word character in i */
			
//			for(int k = 0; k < maxutflen; k++)
//				fprint(2, "%d", nondelim[k]);
//			fprint(2,"\n");
			c += consumed;
		}
		rows = rows->next;
	}

	if(nrows == 2)	/* 2 rows => cannot detect one space delim */
		for(i = 0; i < maxutflen - 2; ++i)
			if(nondelim[i] && !nondelim[i+1] && nondelim[i+2])
				nondelim[i+1] = 1;
	
	list = (Column *)malloc(sizeof(Column));
	list->size = 0;
	list->width = 0;
	list->next = nil;
	last = list;
//	fprint(2, "detectColumns: nondelim[");
	for(i = 0; i < maxutflen - 1; ++i){
//		fprint(2, "%d", nondelim[i]);
		++last->size;
		if(nondelim[i])
			last->width += runewidths[i];
		if(!nondelim[i] && nondelim[i+1]){
			/* a new column starts at i+1 */
			last->width += tabwidth - (last->width % tabwidth);
			last->next = (Column *)malloc(sizeof(Column));
			last = last->next;
			last->size = 0;
			last->width = 0;
			last->next = nil;
		}
	}
//	fprint(2, "%d]\n", nondelim[i]);

	if(last == list)
		return nil;	/* 1 column => not a table */

	/* round columns' width to tabstop */
	

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
			row->line->type = Text;
			writeLine(bp, row->line);
			row = row->next;
		}
		return;
	}else{
		while(row){
			col = table->columns;

			debugLine(row->line, "writeTable");
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

				if(i == col->size){
					while(w < col->width){
						w += tabwidth - w % tabwidth;
						Bputc(bp, '\t');
					}
/*
					w = col->width - w;
					while(w > 0){
						Bputc(bp, '\t');
						w -= tabwidth;
					}
*/
					i = 0;
					w = 0;
					pendingspaces = 0;
					col = col->next;
				}
				consumed = chartorune(&r, c);

				if(r == ' ')
					++pendingspaces;
				else{
//					fprint(2, "found %C, ps %d\n", r, pendingspaces);
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



	debugTable();
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
		fprint(2, "zerowidth = %d tabwidth = %d\n", zerowidth, tabwidth);
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
	//fprint(2, "tabstop = %d\n\n", tabstop);

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