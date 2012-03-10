////////////////////////////////////////////////////////////////////////
/// \file  View2D.cxx
/// \brief A collection of drawable 2-D objects
///
/// \version $Id: View2D.cxx,v 1.4 2012-03-10 06:40:29 bckhouse Exp $
/// \author  messier@indiana.edu
////////////////////////////////////////////////////////////////////////
#include <algorithm>
#include "EventDisplayBase/View2D.h"
#include "EventDisplayBase/Functors.h"
using namespace evdb;

//......................................................................

// All of these static lists are "leaked" when the application ends. But that's
// OK: they were serving a useful purpose right up until that moment, and ROOT
// object destruction takes an age, so the event display actually shuts down
// much faster this way.
std::list<TMarker*>     View2D::fgMarkerL;
std::list<TPolyMarker*> View2D::fgPolyMarkerL;
std::list<TLine*>       View2D::fgLineL;
std::list<TPolyLine*>   View2D::fgPolyLineL;
std::list<TArc*>        View2D::fgArcL;
std::list<TBox*>        View2D::fgBoxL;
std::list<TText*>       View2D::fgTextL;
std::list<TLatex*>      View2D::fgLatexL;

//......................................................................

View2D::View2D() 
{
}

//......................................................................

View2D::~View2D() 
{
  // Make sure to return all our objects to where they came from
  Clear();
}

//......................................................................

void View2D::Draw()
{
  for_each(fArcL.begin(),       fArcL.end(),       draw_tobject());
  for_each(fBoxL.begin(),       fBoxL.end(),       draw_tobject());
  for_each(fPolyLineL.begin(),  fPolyLineL.end(),  draw_tobject());
  for_each(fLineL.begin(),      fLineL.end(),      draw_tobject());
  for_each(fMarkerL.begin(),    fMarkerL.end(),    draw_tobject());
  for_each(fPolyMarkerL.begin(),fPolyMarkerL.end(),draw_tobject());
  for_each(fTextL.begin(),      fTextL.end(),      draw_tobject());
  for_each(fLatexL.begin(),     fLatexL.end(),     draw_tobject());
}

//......................................................................

void View2D::Clear() 
{
  // Empty each of our lists, appending them back onto the static ones
  fgMarkerL.splice(fgMarkerL.end(), fMarkerL);
  fgArcL.splice(fgArcL.end(), fArcL);
  fgBoxL.splice(fgBoxL.end(), fBoxL);
  fgPolyLineL.splice(fgPolyLineL.end(), fPolyLineL);
  fgLineL.splice(fgLineL.end(), fLineL);
  fgPolyMarkerL.splice(fgPolyMarkerL.end(), fPolyMarkerL);
  fgTextL.splice(fgTextL.end(), fTextL);
  fgLatexL.splice(fgLatexL.end(), fLatexL);
}

//......................................................................

TMarker& View2D::AddMarker(double x, double y, int c, int st, double sz)
{
  // Each "Add" function follows this same pattern. If there are no cached
  // objects of the right type we make a new one as instructed. If there are
  // some in the cache, we take possession of one and reset it to the state
  // this new caller wants.

  TMarker* m = 0;
  if(fgMarkerL.empty()){
    m = new TMarker(x,y,st);
    m->SetBit(kCanDelete,kFALSE);
    m->SetMarkerColor(c);
    m->SetMarkerSize(sz);
  }
  else{
    m = fgMarkerL.back();
    fgMarkerL.pop_back();

    m->SetX(x);
    m->SetY(y);
    m->SetMarkerSize(sz);
    m->SetMarkerColor(c);
    m->SetMarkerStyle(st);
  }

  // In either case, we have to remember we have it so that we can give it back
  // when we're done with it.
  fMarkerL.push_back(m);
  return *m;
}

//......................................................................

TPolyMarker& View2D::AddPolyMarker(int n, int c, int st, double sz)
{
  TPolyMarker* pm = 0;
  if(fgPolyMarkerL.empty()){
    pm = new TPolyMarker(n);
    pm->SetBit(kCanDelete,kFALSE);
    pm->SetMarkerColor(c);
    pm->SetMarkerStyle(st);
    pm->SetMarkerSize(sz);
  }
  else {
    pm = fgPolyMarkerL.back();
    fgPolyMarkerL.pop_back();

    // The first call to SetPolyMarker with the 0
    // deletes the current set of points before trying
    // to make a new set
    pm->SetPolyMarker(0);
    pm->SetPolyMarker(n);
    pm->SetMarkerColor(c);
    pm->SetMarkerSize(sz);
    pm->SetMarkerStyle(st);
  }

  fPolyMarkerL.push_back(pm);
  return *pm;
}

//......................................................................

TLine& View2D::AddLine(double x1, double y1, double x2, double y2)
{
  TLine* ln = 0;
  if(fgLineL.empty()){
    ln = new TLine(x1,y1,x2,y2);
    ln->SetBit(kCanDelete,kFALSE);
  }
  else {
    ln = fgLineL.back();
    fgLineL.pop_back();

    ln->SetX1(x1);
    ln->SetY1(y1);
    ln->SetX2(x2);
    ln->SetY2(y2);
  }

  fLineL.push_back(ln);
  return *ln;
}

//......................................................................

TPolyLine& View2D::AddPolyLine(int n, int c, int w, int s)
{
  TPolyLine* pl = 0;
  if(fgPolyLineL.empty()){
    pl = new TPolyLine(n);
    pl->SetBit(kCanDelete,kFALSE);
    pl->SetLineColor(c);
    pl->SetLineWidth(w);
    pl->SetLineStyle(s);
  }
  else {
    pl = fgPolyLineL.back();
    fgPolyLineL.pop_back();

    pl->SetPolyLine(0);
    pl->SetPolyLine(n); // reset elements in PolyLine
    pl->SetOption("");
    pl->SetLineColor(c);
    pl->SetLineWidth(w);
    pl->SetLineStyle(s);
  }

  fPolyLineL.push_back(pl);
  return *pl;
}

//......................................................................

TArc& View2D::AddArc(double x, double y, double r, double p1, double p2) 
{
  TArc* a = 0;
  if(fgArcL.empty()){
    a = new TArc(x,y,r,p1,p2);
    a->SetBit(kCanDelete,kFALSE);
  }
  else {
    a = fgArcL.back();
    fgArcL.pop_back();

    a->SetX1(x);
    a->SetY1(y);
    a->SetR1(r);
    a->SetR2(r);
    a->SetPhimin(p1);
    a->SetPhimax(p2);
  }

  fArcL.push_back(a);
  return *a;
}

//......................................................................

TBox& View2D::AddBox(double x1, double y1, double x2, double y2)
{
  TBox* b = 0;
  if(fgBoxL.empty()){
    b = new TBox(x1,y1,x2,y2);
    b->SetBit(kCanDelete,kFALSE);
  }
  else {
    b = fgBoxL.back();
    fgBoxL.pop_back();

    b->SetX1(x1);
    b->SetY1(y1);
    b->SetX2(x2);
    b->SetY2(y2);
  }

  fBoxL.push_back(b);
  return *b;
}

//......................................................................

TText& View2D::AddText(double x, double y, const char* text)
{
  TText* itxt = 0;
  if(fgTextL.empty()) {
    itxt = new TText(x,y,text);
    itxt->SetBit(kCanDelete,kFALSE);
  }
  else {
    itxt = fgTextL.back();
    fgTextL.pop_back();

    itxt->SetText(x,y,text);
  }

  fTextL.push_back(itxt);
  return *itxt;
}

//......................................................................

TLatex& View2D::AddLatex(double x, double y, const char* text)
{
  TLatex* itxt = 0;
  if(fgLatexL.empty()){
    itxt = new TLatex(x,y,text);
    itxt->SetBit(kCanDelete,kFALSE);
  }
  else {
    itxt = fgLatexL.back();
    fgLatexL.pop_back();

    itxt->SetText(x,y,text);
  }

  fLatexL.push_back(itxt);
  return *itxt;
}

////////////////////////////////////////////////////////////////////////
