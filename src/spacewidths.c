#include <u.h>
#include <libc.h>
#include <bio.h>
#include <draw.h>

// openfont(2) runestringnwidth(2) /sys/src/
#define USAGE "usage: %s [-t tabstop] [-f fontfile]\n"

Font *font;

static void
usage(char *error)
{
	if(error != nil)
		fprint(2, "invalid argument: %s\n", error);
	fprint(2, USAGE, argv0);
	exits("usage");
}

void
main(int argc, char **argv){
	Biobuf bin, bout;
	char *s;
	long ch;
	Rune c;
	int tabstop, rspc;
  
	font = nil;
	if(s = getenv("tabstop")){
		tabstop = atoi(s);
		free(s);
	} else {
		tabstop = 8;
	}

	ARGBEGIN{
	case 't':
		s=EARGF(usage("missing tabstop size."));
		tabstop=atoi(s);
		if(tabstop < 2)
			usage("tabstop must be a greater than 1.");
		break;
	case 'f':
		s=EARGF(usage("font file not provided."));
		font = openfont(nil, s);
		if(font == nil)
			usage("can't open font file.");
		break;
	default:
		usage(nil);
	}ARGEND;
	
	if(font == nil) {
		s = getenv("font");
		if (s == nil)
			usage("either $font or -f is required.");
		font = openfont(nil, s);
		free(s);
	}

	char buf[16];

/*
	print("simple space ] [\n", (Rune)8192);
	
	print("U+2001 ]%C[\n", (Rune)8193);
	print("U+2002 ]%C[\n", (Rune)8194);
	print("U+2003 ]%C[\n", (Rune)8195);
	print("U+2004 ]%C[\n", (Rune)8196);
	print("U+2005 ]%C[\n", (Rune)8197);
	print("U+2006 ]%C[\n", (Rune)8198);
	print("U+2007 ]%C[\n", (Rune)8199);
*/
	for(int i = 8192; i <= 8202; ++i) {
		sprint(buf, "%C", (Rune)i);
		int w = stringwidth(font, buf);
		print("U+%d ]%C[ (%d px)\n", i - 6192, (Rune)i, w);
	}
	exits(nil);
}