#ifndef CDEFHANDLER_H
#define CDEFHANDLER_H

#include "SDL.h"
#include "CSemiDefHandler.h"

class CDefEssential;

struct BMPHeader
{
	int fullSize, _h1, _h2, _h3, _c1, _c2, _c3, _c4, x, y,
		dataSize1, dataSize2; //DataSize=X*Y+2*Y
	unsigned char _c5[8];
	void print(std::ostream & out);
	BMPHeader(){_h1=_h2=0;for(int i=0;i<8;i++)_c5[i]=0;};
};/*
struct DEFHeader
{
	int type, width, height, groups;
	unsigned char palette[767];
};
struct DEFGroup
{
	int ID, items, unknown1, unknown2;
};*/

struct BMPPalette
{
	unsigned char R,G,B,F;
};

class CDefHandler
{
private:
	int totalEntries, DEFType, totalBlocks, fullWidth, fullHeight;
	bool allowRepaint;
	int length;
	BMPPalette palette[256];
	unsigned int * RWEntries;
	int * RLEntries;
	struct SEntry
	{
		std::string name;
		int offset;
		int group;
	} ;
	std::vector<SEntry> SEntries ;
	char id[2];


public:
	std::string defName, curDir;
	std::vector<Cimage> ourImages;
	bool alphaTransformed;
	bool notFreeImgs;
	
	CDefHandler();
	~CDefHandler();
	static void print (std::ostream & stream, int nr, int bytcon);
	int readNormalNr (int pos, int bytCon, unsigned char * str=NULL, bool cyclic=false);
	static unsigned char * CDefHandler::writeNormalNr (int nr, int bytCon);
	SDL_Surface * getSprite (int SIndex, unsigned char * FDef); //zapisuje klatke o zadanym numerze do "testtt.bmp"
	void openDef(std::string name); 
	void expand(unsigned char N,unsigned char & BL, unsigned char & BR);
	void openFromMemory(unsigned char * table, int size, std::string name);
	CDefEssential * essentialize();
};

class CDefEssential //DefHandler with images only
{
public:
	std::vector<Cimage> ourImages;
};

#endif // CDEFHANDLER_H