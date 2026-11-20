#ifndef EREADER_PDF_H
#define EREADER_PDF_H

#include "book.h"

namespace er {

/* Self-contained PDF subset renderer: classic xref + xref streams +
   object streams, Flate/ASCIIHex/ASCII85/RunLength with PNG predictors,
   text (standard-14, embedded TrueType, Type0 with ToUnicode), vector
   paths, clipping, DeviceGray/RGB/CMYK/Indexed color, DCT and raw
   images, form XObjects. Everything runs on the Preloader worker. */
class PdfBook : public Book {
	struct Impl;
	Impl* _p;
	string _fname;
	string _title;
public:
	PdfBook(const string& fname);
	~PdfBook();

	bool open();
	int  pageCount();
	graph_t* renderPage(int idx, int w, int h, uint32_t ink);
	string title() { return _title; }
	bool isTxt() { return false; }
};

}

#endif