#include <u.h>
#include <libc.h>
#include <bio.h>
#include <draw.h>

// openfont(2) runestringnwidth(2)
Font *font;

static void
usage(char *error)
{
	fprint(2, "usage: %s [-t tabstop] [-f fontfile]\n", argv0);
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

	if(f = getenv("tabstop")){
		tabstop = atoi(f);
		free(f);
	} else {
		tabstop = 8;
	}
	

	exits(nil);
}