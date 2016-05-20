/*
  @copyright Steve Keen 2015
  @author Russell Standish
  This file is part of Minsky.

  Minsky is free software: you can redistribute it and/or modify it
  under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  Minsky is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with Minsky.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "group.h"
#include "wire.h"
#include "operation.h"
#include "minsky.h"
#include "cairoItems.h"
#include <cairo_base.h>
#include <ecolab_epilogue.h>
using namespace std;
using namespace ecolab::cairo;

namespace minsky
{
  Group& GroupPtr::operator*() const {return dynamic_cast<Group&>(ItemPtr::operator*());}
  Group* GroupPtr::operator->() const {return dynamic_cast<Group*>(ItemPtr::operator->());}

  SVGRenderer Group::svgRenderer;

  ItemPtr Group::removeItem(const Item& it)
  {
    for (auto i=items.begin(); i!=items.end(); ++i)
      if (i->get()==&it)
        {
          ItemPtr r=*i;
          items.erase(i);
          return r;
        }

    for (auto& g: groups)
      if (ItemPtr r=g->removeItem(it))
        return r;
    return ItemPtr();
  }
       
  WirePtr Group::removeWire(const Wire& w)
  {
    for (auto i=wires.begin(); i!=wires.end(); ++i)
      if (i->get()==&w)
        {
          WirePtr r=*i;
          wires.erase(i);
          return r;
        }

    for (auto& g: groups)
      if (WirePtr r=g->removeWire(w))
        return r;
    return WirePtr();
  }

  GroupPtr Group::removeGroup(const Group& group)
  {
    for (auto i=groups.begin(); i!=groups.end(); ++i)
      if (i->get()==&group)
        {
          GroupPtr r=*i;
          groups.erase(i);
          return r;
        }

    for (auto& g: groups)
      if (GroupPtr r=g->removeGroup(group))
        return r;
    return GroupPtr();
  }
       
  ItemPtr Group::findItem(const Item& it) const 
  {
    // start by looking in the group it thnks it belongs to
    if (auto g=it.group.lock())
      if (g.get()!=this) 
        {
          auto i=g->findItem(it);
          if (i) return i;
        }
    return findAny(&Group::items, [&](const ItemPtr& x){return x.get()==&it;});
  }


  ItemPtr Group::addItem(const shared_ptr<Item>& it)
  {
    if (auto x=dynamic_pointer_cast<Group>(it))
      return addGroup(x);
   
    // stash position
    float x=it->x(), y=it->y();
    auto origGroup=it->group.lock();

    if (origGroup.get()==this) return it; // nothing to do.
    if (origGroup)
      origGroup->removeItem(*it);

    it->group=self.lock();
    it->moveTo(x,y);

    // move wire to highest common group
    // TODO add in I/O variables if needed
    for (auto& p: it->ports)
      {
        assert(p);
        for (auto& w: p->wires)
          {
            assert(w);
            adjustWiresGroup(*w);
          }
      }

    // need to deal with integrals especially because of the attached variable
    if (auto intOp=dynamic_cast<IntOp*>(it.get()))
      if (intOp->intVar)
        if (auto oldG=intOp->intVar->group.lock())
          {
            if (oldG.get()!=this)
              addItem(oldG->removeItem(*intOp->intVar));
          }
        else
          addItem(intOp->intVar);
            
    items.push_back(it);
    return items.back();
  }

  void Group::adjustWiresGroup(Wire& w)
  {
    // Find common ancestor group, and move wire to it
    assert(w.from() && w.to());
    shared_ptr<Group> p1=w.from()->item.group.lock(), p2=w.to()->item.group.lock();
    assert(p1 && p2);
    unsigned l1=p1->level(), l2=p2->level();
    for (; l1>l2; l1--) p1=p1->group.lock();
    for (; l2>l1; l2--) p2=p2->group.lock();
    while (p1!=p2) 
      {
        assert(p1 && p2);
        p1=p1->group.lock();
        p2=p2->group.lock();
      }
    w.moveIntoGroup(*p1);
  }

  void Group::moveContents(Group& source) {
     if (&source!=this)
       {
         for (auto& i: source.groups)
           if (i->higher(*this))
             throw error("attempt to move a group into itself");
          for (auto& i: source.items)
            addItem(i);
          for (auto& i: source.groups)
            addGroup(i);
          /// no need to move wires, as these are handled above
          source.clear();
       }
  }

  namespace 
  {
    bool nocycles(const Group& g)
    {
      set<const Group*> sg;
      sg.insert(&g);
      for (auto i=g.group.lock(); i; i=i->group.lock())
        if (!sg.insert(i.get()).second)
          return false;
      return true;
    }
  }

  GroupPtr Group::addGroup(const std::shared_ptr<Group>& g)
  {
    auto origGroup=g->group.lock();
    if (origGroup.get()==this) return g; // nothing to do
    if (origGroup)
      origGroup->removeGroup(*g);
    g->group=self;
    g->self=g;
    groups.push_back(g);
    assert(nocycles(*this));
    return groups.back();
  }

  WirePtr Group::addWire(const std::shared_ptr<Wire>& w)
  {
    assert(w->from() && w->to());
    assert(nocycles(*w->from()->item.group.lock()));
    assert(nocycles(*w->to()->item.group.lock()));
    wires.push_back(w);
    return wires.back();
  }

  bool Group::higher(const Group& x) const
  {
    //if (!x) return false; // global group x is always higher
    for (auto i: groups)
      if (i.get()==&x) return true;
    for (auto i: groups)
      if (higher(*i))
        return true;
    return false;
  }

  unsigned Group::level() const
  {
    assert(nocycles(*this));
    unsigned l=0;
    for (auto i=group.lock(); i; i=i->group.lock()) l++;
    return l;
  }

  namespace
  {
    template <class G>
    G& globalGroup(G& start)
    {
      auto g=&start;
      while (auto g1=g->group.lock())
        g=g1.get();
      return *g;
    }
  }

  const Group& Group::globalGroup() const
  {return minsky::globalGroup(*this);}
  Group& Group::globalGroup()
  {return minsky::globalGroup(*this);}


  bool Group::uniqueItems(set<void*>& idset) const
  {
    for (auto& i: items)
      if (!idset.insert(i.get()).second) return false;
    for (auto& i: wires)
      if (!idset.insert(i.get()).second) return false;
    for (auto& i: groups)
      if (!idset.insert(i.get()).second || !i->uniqueItems(idset)) 
        return false;
    return true;
  }

  float Group::contentBounds(double& x0, double& y0, double& x1, double& y1) const
  {
    float localZoom=1;
#ifndef CAIRO_HAS_RECORDING_SURFACE
#error "Please upgrade your cairo to a version implementing recording surfaces"
#endif
    SurfacePtr surf
      (new Surface
       (cairo_recording_surface_create(CAIRO_CONTENT_COLOR_ALPHA,NULL)));
    for (auto& i: items)
      try 
        {
          i->draw(surf->cairo());  
          localZoom=i->zoomFactor;
        }
      catch (const std::exception& e) 
        {cerr<<"illegal exception caught in draw()"<<e.what()<<endl;}
      catch (...) {cerr<<"illegal exception caught in draw()";}
    cairo_recording_surface_ink_extents(surf->surface(),
                                        &x0,&y0,&x1,&y1);

    for (auto& i: groups)
      {
        float w=0.5f*i->width*i->zoomFactor,
          h=0.5f*i->height*i->zoomFactor;
        x0=min(i->x()-0.5*w, x0);
        x1=max(i->x()+0.5*w, x1);
        y0=min(i->y()-0.5*h, y0);
        y1=max(i->x()+0.5*h, y1);
      }


    // if there are no contents, result is not finite. In this case,
    // set the content bounds to a 10x10 sized box around the centroid of the I/O variables.

    if (x0==numeric_limits<float>::max())
      {
        // TODO!
//        float cx=0, cy=0;
//        for (int i: inVariables)
//          {
//            cx+=cminsky().variables[i]->x();
//            cy+=cminsky().variables[i]->y();
//          }
//        for (int i: outVariables)
//          {
//            cx+=cminsky().variables[i]->x();
//            cy+=cminsky().variables[i]->y();
//          }
//        int n=inVariables.size()+outVariables.size();
//        cx/=n;
//        cy/=n;
//        x0=cx-10;
//        x1=cx+10;
//        y0=cy-10;
//        y1=cy+10;
      }
    else
      {
        // extend width by 2 pixels to allow for the slightly oversized variable icons
        x0-=2*this->localZoom();
        y0-=2*this->localZoom();
        x1+=2*this->localZoom();
        y1+=2*this->localZoom();
      }

    return localZoom;
  }

  float Group::computeDisplayZoom()
  {
    double x0, x1, y0, y1;
    float l, r;
    float lz=contentBounds(x0,y0,x1,y1);
    x0=min(x0,double(x()));
    x1=max(x1,double(x()));
    y0=min(y0,double(y()));
    y1=max(y1,double(y()));
    // first compute the value assuming margins are of zero width
    displayZoom = 2*max( max(x1-x(), x()-x0)/width, max(y1-y(), y()-y0)/height );

    // account for shrinking margins
    float readjust=zoomFactor/edgeScale() / (displayZoom>1? displayZoom:1);
    //TODO    margins(l,r);
    l*=readjust; r*=readjust;
    displayZoom = max(displayZoom, 
                      float(max((x1-x())/(0.5f*width-r), (x()-x0)/(0.5f*width-l))));
  
    // displayZoom*=1.1*rotFactor()/lz;

    // displayZoom should never be less than 1
    displayZoom=max(displayZoom, 1.0f);
    return displayZoom;
  }

  const Group* Group::minimalEnclosingGroup(float x0, float y0, float x1, float y1) const
  {
    if (x0>x()-0.5*width || x1<x()+0.5*width || 
        y0>y()-0.5*height || y1<y()+0.5*height)
      return nullptr;
    // at this point, this is a candidate. Check if any child groups are also
    for (auto& g: groups)
      if (auto mg=g->minimalEnclosingGroup(x0,y0,x1,y1))
        return mg;
    return this;
  }

  Group* Group::minimalEnclosingGroup(float x0, float y0, float x1, float y1)
  {
    if (x0>x()-0.5*width || x1<x()+0.5*width || 
        y0>y()-0.5*height || y1<y()+0.5*height)
      return nullptr;
    // at this point, this is a candidate. Check if any child groups are also
    for (auto& g: groups)
      if (auto mg=g->minimalEnclosingGroup(x0,y0,x1,y1))
        return mg;
    return this;
  }

  void Group::setZoom(float factor)
  {
    zoomFactor=factor;
    computeDisplayZoom();
    float lzoom=localZoom();
    for (auto& i: items)
      i->zoomFactor=lzoom;
    for (auto& i: groups)
      i->setZoom(lzoom);
  }

  namespace {
    inline float sqr(float x) {return x*x;}
  }

  ClosestPort::ClosestPort(const Group& g, InOut io, float x, float y)
  {
    float minr2=std::numeric_limits<float>::max();
    g.recursiveDo(&Group::items, [&](const Items& m, Items::const_iterator i)
                  {
                    for (auto& p: (*i)->ports)
                      if (io!=out && p->input() || io!=in && !p->input())
                        {
                          float r2=sqr(p->x()-x)+sqr(p->y()-y);
                          if (r2<minr2)
                            {
                              shared_ptr<Port>::operator=(p);
                              minr2=r2;
                            }
                        }
                    return false;
                  });
  }

  void Group::draw(cairo_t* cairo) const
  {
    double angle=rotation * M_PI / 180.0;

    // determine how big the group icon should be to allow
    // sufficient space around the side for the edge variables
    float leftMargin, rightMargin;
    margins(leftMargin, rightMargin);
    leftMargin*=zoomFactor; rightMargin*=zoomFactor;

    unsigned width=zoomFactor*this->width, height=zoomFactor*this->height;
    // bitmap needs to be big enough to allow a rotated
    // icon to fit on the bitmap.
    float rotFactor=this->rotFactor();


    // set clip to indicate icon boundary
    cairo_rotate(cairo, angle);
    cairo_rectangle(cairo,-0.5*width,-0.5*height,width,height);
    cairo_clip(cairo);

   // draw default group icon
    cairo_save(cairo);
    //    cairo_rotate(cairo, angle);

    // display I/O region in grey
    //updatePortLocation();
    drawIORegion(cairo);

    cairo_translate(cairo, -0.5*width+leftMargin, -0.5*height);


              
    double scalex=double(width-leftMargin-rightMargin)/width;
    cairo_scale(cairo, scalex, 1);

    // draw a simple frame 
    cairo_rectangle(cairo,0,0,width,height);
    cairo_save(cairo);
    cairo_identity_matrix(cairo);
    cairo_set_line_width(cairo,1);
    cairo_stroke(cairo);
    cairo_restore(cairo);

    if (!displayContents())
      {
        cairo_scale(cairo,width/svgRenderer.width(),height/svgRenderer.height());
        cairo_rectangle(cairo,0, 0,svgRenderer.width(), svgRenderer.height());
        cairo_clip(cairo);
        svgRenderer.render(cairo);
      }
    cairo_restore(cairo);

    cairo_identity_matrix(cairo);

    drawEdgeVariables(cairo);


    // display text label
    if (!title.empty())
      {
        cairo_save(cairo);
        cairo_identity_matrix(cairo);
        cairo_scale(cairo, zoomFactor, zoomFactor);
        cairo_select_font_face
          (cairo, "sans-serif", CAIRO_FONT_SLANT_ITALIC, 
           CAIRO_FONT_WEIGHT_NORMAL);
        cairo_set_font_size(cairo,12);
              
        // extract the bounding box of the text
        cairo_text_extents_t bbox;
        cairo_text_extents(cairo,title.c_str(),&bbox);
        double w=0.5*bbox.width+2; 
        double h=0.5*bbox.height+5;
        double fm=std::fmod(rotation,360);

        // if rotation is in 1st or 3rd quadrant, rotate as
        // normal, otherwise flip the text so it reads L->R
        if ((fm>-90 && fm<90) || fm>270 || fm<-270)
          cairo_rotate(cairo, angle);
        else
          cairo_rotate(cairo, angle+M_PI);
 
        // prepare a background for the text, partially obscuring graphic
        double transparency=displayContents()? 0.25: 1;
        cairo_set_source_rgba(cairo,0,1,1,0.5*transparency);
        cairo_rectangle(cairo,-w,-h,2*w,2*h);
        cairo_fill(cairo);

        // display text
        cairo_move_to(cairo,-w+1,h-4);
        cairo_set_source_rgba(cairo,0,0,0,transparency);
        cairo_show_text(cairo,title.c_str());
        cairo_restore(cairo);
      }

    cairo_identity_matrix(cairo);

    // shouldn't be needed??
    // set clip to indicate icon boundary
    cairo_rotate(cairo, angle);
    cairo_rectangle(cairo,-0.5*width,-0.5*height,width,height);
    cairo_clip(cairo);

    if (mouseFocus)
      drawPorts(cairo);

    if (selected) drawSelected(cairo);
  }

  void Group::drawEdgeVariables(cairo_t* cairo) const
  {
    cairo_save(cairo);
    for (auto& i: inVariables)
      {
        cairo_identity_matrix(cairo);
        cairo_translate(cairo,i->x()-x(),i->y()-y());
        i->draw(cairo);
      }
    for (auto& i: outVariables)
      {
        cairo_identity_matrix(cairo);
        cairo_translate(cairo,i->x()-x(),i->y()-y());
        i->draw(cairo);
      }
    cairo_restore(cairo);
  }

  // draw notches in the I/O region to indicate docking capability
  void Group::drawIORegion(cairo_t* cairo) const
  {
    cairo_save(cairo);
    float left, right;
    margins(left,right);
    left*=zoomFactor;
    right*=zoomFactor;
    float y=0, dy=5*edgeScale();
    for (auto& i: inVariables)
      y=max(y, fabs(i->y()-this->y())+3*dy);
    cairo_set_source_rgba(cairo,0,1,1,0.5);
    float w=0.5*zoomFactor*width, h=0.5*zoomFactor*height;

    cairo_move_to(cairo,-w,-h);
    // create notch in input region
    cairo_line_to(cairo,-w,y-dy);
    cairo_line_to(cairo,left-w-2,y-dy);
    cairo_line_to(cairo,left-w,y);
    cairo_line_to(cairo,left-w-2,y+dy);
    cairo_line_to(cairo,-w,y+dy);
    cairo_line_to(cairo,-w,h);
    cairo_line_to(cairo,left-w,h);
    cairo_line_to(cairo,left-w,-h);
    cairo_close_path(cairo);
    cairo_fill(cairo);

    y=0;
    for (auto& i: outVariables)
      y=max(y, fabs(i->y()-this->y())+3*dy);
    cairo_move_to(cairo,w,-h);
    // create notch in output region
    cairo_line_to(cairo,w,y-dy);
    cairo_line_to(cairo,w-right,y-dy);
    cairo_line_to(cairo,w-right+2,y);
    cairo_line_to(cairo,w-right,y+dy);
    cairo_line_to(cairo,w,y+dy);
    cairo_line_to(cairo,w,h);
    cairo_line_to(cairo,w-right,h);
    cairo_line_to(cairo,w-right,-h);
    cairo_close_path(cairo);
    cairo_fill(cairo);

    cairo_restore(cairo);
  }


  void Group::margins(float& left, float& right) const
  {
    float scale=edgeScale()/zoomFactor;
    left=right=10*scale;
    for (auto& i: inVariables)
      {
        float w= scale * (2*RenderVariable(*i).width()+2);
        assert(i->type()!=VariableType::undefined);
        if (w>left) left=w;
      }
    for (auto& i: outVariables)
      {
        float w= scale * (2*RenderVariable(*i).width()+2);
        assert(i->type()!=VariableType::undefined);
        if (w>right) right=w;
      }
  }

  float Group::rotFactor() const
  {
    float rotFactor;
    float ac=abs(cos(rotation*M_PI/180));
    static const float invSqrt2=1/sqrt(2);
    if (ac>=invSqrt2) 
      rotFactor=1.15/ac; //use |1/cos(angle)| as rotation factor
    else
      rotFactor=1.15/sqrt(1-ac*ac);//use |1/sin(angle)| as rotation factor
    return rotFactor;
  }


}