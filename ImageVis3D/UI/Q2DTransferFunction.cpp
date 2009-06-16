/*
   For more information, please see: http://software.sci.utah.edu

   The MIT License

   Copyright (c) 2008 Scientific Computing and Imaging Institute,
   University of Utah.


   Permission is hereby granted, free of charge, to any person obtaining a
   copy of this software and associated documentation files (the "Software"),
   to deal in the Software without restriction, including without limitation
   the rights to use, copy, modify, merge, publish, distribute, sublicense,
   and/or sell copies of the Software, and to permit persons to whom the
   Software is furnished to do so, subject to the following conditions:

   The above copyright notice and this permission notice shall be included
   in all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
   OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
   THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
   LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
   FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
   DEALINGS IN THE SOFTWARE.
*/


//!    File   : Q2DTransferFunction.cpp
//!    Author : Jens Krueger
//!             SCI Institute
//!             University of Utah
//!    Date   : July 2008
//
//!    Copyright (C) 2008 SCI Institute

#include <exception>
#include <limits>
#include "Q2DTransferFunction.h"
#include <QtGui/QPainter>

#include "../Tuvok/Controller/MasterController.h"
#include "../Tuvok/Renderer/GPUMemMan/GPUMemMan.h"

#ifdef max
  #undef max
#endif

#ifdef min
  #undef min
#endif

using namespace std;

Q2DTransferFunction::Q2DTransferFunction(MasterController& masterController, QWidget *parent) :
  QTransferFunction(masterController, parent),
  m_pTrans(NULL),
  m_iPaintmode(Q2DT_PAINT_NONE),
  m_iActiveSwatchIndex(-1),
  m_eTransferFunctionMode(TFM_EXPERT),
  m_iCachedHeight(0),
  m_iCachedWidth(0),
  m_pBackdropCache(NULL),
  m_pHistImage(NULL),

  // border size, may be changed arbitrarily
  m_iSwatchBorderSize(3),

  // mouse motion
  m_iPointSelIndex(-1),
  m_iGradSelIndex(-1),
  m_vMousePressPos(0,0),
  m_bDragging(false),
  m_bDraggingAll(false),
  m_eDragMode(DRM_NONE),
  m_vZoomWindow(0.0f,0.0f,1.0f,1.0f),
  m_eSimpleDragMode(SDM_NONE),
  m_iSimpleDragModeSubindex(0)
{
  SetColor(isEnabled());

  setFocusPolicy(Qt::StrongFocus);
}

Q2DTransferFunction::~Q2DTransferFunction(void)
{
  // delete the cache pixmap and image
  delete m_pBackdropCache;
  delete m_pHistImage;
}

QSize Q2DTransferFunction::minimumSizeHint() const
{
  return QSize(50, 50);
}

QSize Q2DTransferFunction::sizeHint() const
{
  return QSize(400, 400);
}

void Q2DTransferFunction::SetData(const Histogram2D* vHistogram, TransferFunction2D* pTrans) {
  m_pTrans = pTrans;
  if (m_pTrans == NULL) return;

  // resize the histogram vector
  m_vHistogram.Resize(vHistogram->GetSize());

  // if the histogram is empty we are done
  if (m_vHistogram.GetSize().area() == 0)  return;

  // rescale the histogram to the [0..1] range
  // first find min and max ...
  unsigned int iMax = vHistogram->GetLinear(0);
  unsigned int iMin = iMax;
  for (size_t i = 0;i<m_vHistogram.GetSize().area();i++) {
    unsigned int iVal = vHistogram->GetLinear(i);
    if (iVal > iMax) iMax = iVal;
    if (iVal < iMin) iMin = iVal;
  }

  // ... than rescale
  float fDiff = float(iMax)-float(iMin);
  for (size_t i = 0;i<m_vHistogram.GetSize().area();i++)
    m_vHistogram.SetLinear(i, (float(vHistogram->GetLinear(i)) - float(iMin)) / fDiff);

  // Upload the new TF to the GPU.
  m_MasterController.MemMan()->Changed2DTrans(NULL, m_pTrans);

  // force the draw routine to recompute the backdrop cache
  m_bHistogramChanged = true;

  emit SwatchChange();
}


void Q2DTransferFunction::GenerateHistogramImage() {
  if (m_pTrans == NULL) return;

  // convert the histogram into an image
  // define the bitmap ...
  if (!m_pHistImage || 
      static_cast<size_t>(m_pHistImage->height()) != m_vHistogram.GetSize().x ||
      static_cast<size_t>(m_pHistImage->width()) != m_vHistogram.GetSize().y) {
    delete m_pHistImage;
    m_pHistImage = new QImage(QSize(int(m_vHistogram.GetSize().x), int(m_vHistogram.GetSize().y)), QImage::Format_RGB32);
  }

  for (size_t y = 0;y<m_vHistogram.GetSize().y;y++)
    for (size_t x = 0;x<m_vHistogram.GetSize().x;x++) {
      float value = min<float>(1.0f, pow(m_vHistogram.Get(x,y),1.0f/(1+(m_fHistfScale-1)/100.0f)));
      m_pHistImage->setPixel(int(x),
         int(m_vHistogram.GetSize().y-(y+1)),
         qRgb(int(m_colorBack.red()  * (1.0f-value) +
                  m_colorHistogram.red()  * value),
              int(m_colorBack.green()* (1.0f-value) +
                  m_colorHistogram.green()* value),
              int(m_colorBack.blue() * (1.0f-value) +
                  m_colorHistogram.blue() * value)));
    }

  m_bBackdropCacheUptodate = false;
  m_bHistogramChanged = false;
}

void Q2DTransferFunction::DrawHistogram(QPainter& painter) {
  if (m_pTrans == NULL) return;

  if (m_bHistogramChanged) GenerateHistogramImage();

  // ... draw it
  QRectF target(0, 0,
    painter.viewport().width(), painter.viewport().height());
  QRectF source(m_vZoomWindow.x * m_vHistogram.GetSize().x,
                m_vZoomWindow.y * m_vHistogram.GetSize().y,
                m_vZoomWindow.z * m_vHistogram.GetSize().x,
                m_vZoomWindow.w * m_vHistogram.GetSize().y);
  painter.drawImage( target, *m_pHistImage, source );
}

INTVECTOR2 Q2DTransferFunction::Normalized2Offscreen(FLOATVECTOR2 vfCoord) const {
  return INTVECTOR2(int(-m_vZoomWindow.x/m_vZoomWindow.z * m_iCachedWidth +vfCoord.x/m_vZoomWindow.z*m_iCachedWidth),
                    int(-m_vZoomWindow.y/m_vZoomWindow.w * m_iCachedHeight+vfCoord.y/m_vZoomWindow.w*m_iCachedHeight));
}

INTVECTOR2 Q2DTransferFunction::Normalized2Screen(FLOATVECTOR2 vfCoord) const {
  return INTVECTOR2(int(-m_vZoomWindow.x/m_vZoomWindow.z * width() +vfCoord.x/m_vZoomWindow.z*width()),
                    int(-m_vZoomWindow.y/m_vZoomWindow.w * height()+vfCoord.y/m_vZoomWindow.w*height()));
}

FLOATVECTOR2 Q2DTransferFunction::Screen2Normalized(INTVECTOR2 vCoord) const {
  return FLOATVECTOR2(float(vCoord.x)*m_vZoomWindow.z/width()+m_vZoomWindow.x,
                      float(vCoord.y)*m_vZoomWindow.w/height()+m_vZoomWindow.y);
}

E2DSimpleModePolyType Q2DTransferFunction::ClassifySwatch(TFPolygon& polygon, FLOATVECTOR2& vPseudoTrisHandle) const {
  // check the most basic properties first (four vertices, linear gradient, and exactly 3 gradient stops)
  if (polygon.pPoints.size() == 4 && !polygon.bRadial && polygon.pGradientStops.size() == 3) {

    // check if the top and bottom edge are parallel to the x-axis
    if (polygon.pPoints[1].y == polygon.pPoints[2].y && polygon.pPoints[0].y == polygon.pPoints[3].y) {

      // check if the left and right edge are parallel to the y-axis -> rectangle
      if (polygon.pPoints[0].x == polygon.pPoints[1].x && polygon.pPoints[2].x == polygon.pPoints[3].x) {
        return PT_RECTANGLE;
      } else {
        // check if the left and right edge are intersecting "near" the x-axis

        double x1 = polygon.pPoints[0].x, y1 = polygon.pPoints[0].y;
        double x2 = polygon.pPoints[1].x, y2 = polygon.pPoints[1].y;
        double x3 = polygon.pPoints[2].x, y3 = polygon.pPoints[2].y;
        double x4 = polygon.pPoints[3].x, y4 = polygon.pPoints[3].y;


        double u = ((x4-x3)*(y1-y3)-(y4-y3)*(x1-x3))/((y4-y3)*(x2-x1)-(x4-x3)*(y2-y1));

        double h = y1 + u *(y2-y1);

        if (fabs(h-1.0f) < 0.01) {
          vPseudoTrisHandle = FLOATVECTOR2(x1 + u *(x2-x1),1.0);
          return PT_PSEUDOTRIS;
        }
      }

    }

  }
  return PT_OTHER;
}

void Q2DTransferFunction::DrawPolygonWithCool3DishBorder(QPainter& painter, std::vector<QPoint>& pointList, QPen& borderPen, QPen& borderPenHighlight) {
  painter.setPen(borderPen); 
  painter.setBrush(Qt::NoBrush);
  painter.drawPolygon(&pointList[0], int(pointList.size()));
  painter.setPen(borderPenHighlight); 
  painter.drawPolygon(&pointList[0], int(pointList.size()));
}

void Q2DTransferFunction::DrawPolyVertex(QPainter& painter, QPoint& p) {
  DrawPolyVertex(painter, INTVECTOR2(p.x(), p.y()));
}

void Q2DTransferFunction::DrawPolyVertex(QPainter& painter, const INTVECTOR2& p) {
  painter.drawEllipse(p.x-m_iSwatchBorderSize, p.y-m_iSwatchBorderSize, m_iSwatchBorderSize*2, m_iSwatchBorderSize*2);
}

void Q2DTransferFunction::DrawSwatcheDecoration(QPainter& painter) {
  if (m_pTrans == NULL) return;

  painter.setRenderHint(painter.Antialiasing, true);
  painter.translate(+0.5, +0.5);  /// \todo check if we need this

  QPen borderPen(m_colorSwatchBorder,         m_iSwatchBorderSize, Qt::SolidLine);
  QPen borderPenHighlight(m_colorSwatchBorderHighlight, m_iSwatchBorderSize/2, Qt::SolidLine);
  QPen borderPenHighlightCenter(m_colorSwatchBorderHighlightCenter, 1, Qt::SolidLine);
  QPen inactiveBorderPen(m_colorSwatchBorderInactive, m_iSwatchBorderSize, Qt::SolidLine);
  QPen inactiveBorderHighlight(m_colorSwatchBorderInactiveHighlight, m_iSwatchBorderSize/2, Qt::SolidLine);
  QPen noBorderPen(Qt::NoPen);
  QPen circlePen(m_colorSwatchBorderCircle, m_iSwatchBorderSize, Qt::SolidLine);
  QPen circlePenHighlight(m_colorSwatchBorderCircleHighlight, m_iSwatchBorderSize/2, Qt::SolidLine);
  QPen gradCircePen(m_colorSwatchGradCircle, m_iSwatchBorderSize/2, Qt::SolidLine);
  QPen circlePenSel(m_colorSwatchBorderCircleSel, m_iSwatchBorderSize, Qt::SolidLine);
  QPen gradCircePenSel(m_colorSwatchGradCircleSel, m_iSwatchBorderSize/2, Qt::SolidLine);

  QBrush solidBrush = QBrush(m_colorSwatchBorderCircle, Qt::SolidPattern);


  // render swatches
  for (size_t i = 0;i<m_pTrans->m_Swatches.size();i++) {
    TFPolygon& currentSwatch = m_pTrans->m_Swatches[i];

    std::vector<QPoint> pointList(currentSwatch.pPoints.size());
    for (size_t j = 0;j<currentSwatch.pPoints.size();j++) {
      INTVECTOR2 vPixelPos = Normalized2Screen(currentSwatch.pPoints[j]);
      pointList[j] = QPoint(vPixelPos.x, vPixelPos.y);
    }


    if (m_eTransferFunctionMode == TFM_BASIC) {
      // for the simple Seg3D like interface to work we classify polygons into three categories: 
      //  1. rectangles
      //  2. the "seg3d triangle" which really is a trapezoid
      //  3. anything else (in particular any polygon that is not a quad)
      
      FLOATVECTOR2 vHandle            = m_vSimpleSwatchInfo[i].m_vHandlePos;
      E2DSimpleModePolyType polyType = m_vSimpleSwatchInfo[i].m_eType;

      if (m_iActiveSwatchIndex == int(i)) 
        DrawPolygonWithCool3DishBorder(painter, pointList, borderPen, borderPenHighlight); 
      else 
        DrawPolygonWithCool3DishBorder(painter,  pointList, inactiveBorderPen, inactiveBorderHighlight);

      switch (polyType) {
        case PT_PSEUDOTRIS : {
                              INTVECTOR2 vPixelPos = Normalized2Screen(vHandle);

                              DrawPolyVertex(painter, pointList[1]);
                              DrawPolyVertex(painter, pointList[2]);

                              pointList[1] = pointList[3];
                              pointList[2] = QPoint(vPixelPos.x, vPixelPos.y);
                              pointList.pop_back();
                              if (m_iActiveSwatchIndex == int(i)) 
                                DrawPolygonWithCool3DishBorder(painter, pointList, borderPen, borderPenHighlight); 
                              else 
                                DrawPolygonWithCool3DishBorder(painter,  pointList, inactiveBorderPen, inactiveBorderHighlight);
                              DrawPolyVertex(painter, vPixelPos);

                             }
                             break;
        case PT_RECTANGLE  : 
                            DrawPolyVertex(painter, pointList[0]);
                            DrawPolyVertex(painter, pointList[1]);
                            DrawPolyVertex(painter, pointList[2]);
                            DrawPolyVertex(painter, pointList[3]);
                            break;
        default : break;
      }


    } else {

      // shape
      if (m_iActiveSwatchIndex == int(i)) 
        DrawPolygonWithCool3DishBorder(painter, pointList, borderPen, borderPenHighlight); 
      else 
        DrawPolygonWithCool3DishBorder(painter,  pointList, inactiveBorderPen, inactiveBorderHighlight);

      // vertices
      painter.setBrush(solidBrush);
      for (size_t j = 0;j<currentSwatch.pPoints.size();j++) {
        if (m_iActiveSwatchIndex == int(i) && m_iPointSelIndex == int(j)) 
          painter.setPen(circlePenSel); 
        else 
          painter.setPen(circlePen);
        DrawPolyVertex(painter,pointList[j]);

        if (m_iActiveSwatchIndex != int(i) || m_iPointSelIndex != int(j)) {
          painter.setPen(circlePenHighlight);
          DrawPolyVertex(painter,pointList[j]);
        }
      }

      // gradient coords
      if (m_iActiveSwatchIndex == int(i)) {
        painter.setBrush(Qt::NoBrush);
        for (int j = 0;j<2;j++) {
          if (m_iGradSelIndex==j) 
            painter.setPen(gradCircePenSel); 
          else 
            painter.setPen(gradCircePen);
          INTVECTOR2 vPixelPos = Normalized2Screen(currentSwatch.pGradientCoords[j])-INTVECTOR2(m_iSwatchBorderSize,m_iSwatchBorderSize);
          DrawPolyVertex(painter, vPixelPos);
        }
      }

    }
  }

  painter.setRenderHint(painter.Antialiasing, false);
}

void Q2DTransferFunction::DrawSwatches(QPainter& painter) {
  if (m_pTrans == NULL) return;

  QPen noBorderPen(Qt::NoPen);
  QPen circlePen(m_colorSwatchBorderCircle, m_iSwatchBorderSize, Qt::SolidLine);
  QPen gradCircePen(m_colorSwatchGradCircle, m_iSwatchBorderSize/2, Qt::SolidLine);
  QPen circlePenSel(m_colorSwatchBorderCircleSel, m_iSwatchBorderSize, Qt::SolidLine);
  QPen gradCircePenSel(m_colorSwatchGradCircleSel, m_iSwatchBorderSize/2, Qt::SolidLine);

  painter.setPen(noBorderPen);

  QBrush solidBrush = QBrush(m_colorSwatchBorderCircle, Qt::SolidPattern);

  // render swatches
  for (size_t i = 0;i<m_pTrans->m_Swatches.size();i++) {
    TFPolygon& currentSwatch = m_pTrans->m_Swatches[i];

    std::vector<QPoint> pointList(currentSwatch.pPoints.size());
    for (size_t j = 0;j<currentSwatch.pPoints.size();j++) {
      INTVECTOR2 vPixelPos = Normalized2Offscreen(currentSwatch.pPoints[j]);
      pointList[j] = QPoint(vPixelPos.x, vPixelPos.y);
    }

    INTVECTOR2 vPixelPos0 = Normalized2Offscreen(currentSwatch.pGradientCoords[0])-INTVECTOR2(m_iSwatchBorderSize, m_iSwatchBorderSize),
		           vPixelPos1 = Normalized2Offscreen(currentSwatch.pGradientCoords[1])-INTVECTOR2(m_iSwatchBorderSize, m_iSwatchBorderSize);

    QGradient* pGradientBrush;
    if (currentSwatch.bRadial) {
      double r = sqrt( pow(double(vPixelPos0.x-vPixelPos1.x),2.0) + pow(double(vPixelPos0.y-vPixelPos1.y),2.0));
      pGradientBrush = new QRadialGradient(vPixelPos0.x, vPixelPos0.y, r);
    } else {
      pGradientBrush = new QLinearGradient(vPixelPos0.x, vPixelPos0.y, vPixelPos1.x, vPixelPos1.y);
    }

    for (size_t j = 0;j<currentSwatch.pGradientStops.size();j++) {
      pGradientBrush->setColorAt(currentSwatch.pGradientStops[j].first,
                   QColor(int(currentSwatch.pGradientStops[j].second[0]*255),
                          int(currentSwatch.pGradientStops[j].second[1]*255),
                          int(currentSwatch.pGradientStops[j].second[2]*255),
                          int(currentSwatch.pGradientStops[j].second[3]*255)));
    }

    painter.setBrush(*pGradientBrush);
    painter.drawPolygon(&pointList[0], int(currentSwatch.pPoints.size()));
    delete pGradientBrush;
    painter.setBrush(Qt::NoBrush);
  }
}

void Q2DTransferFunction::SetDragMode(bool bShiftPressed, bool bCtrlPressed) {
  if (bShiftPressed)
    if(bCtrlPressed)
      m_eDragMode = DRM_ROTATE;
    else
      m_eDragMode = DRM_MOVE;
  else
    if(bCtrlPressed)
      m_eDragMode = DRM_SCALE;
    else
      m_eDragMode = DRM_NONE;

}

void Q2DTransferFunction::keyReleaseEvent(QKeyEvent *event) {
  SetDragMode( event->modifiers() & Qt::ShiftModifier,
               event->modifiers() & Qt::ControlModifier);
  DragInit(m_vMousePressPos, m_mouseButton);
}


void Q2DTransferFunction::keyPressEvent(QKeyEvent *event) {
  SetDragMode( event->modifiers() & Qt::ShiftModifier,
               event->modifiers() & Qt::ControlModifier);
  DragInit(m_vMousePressPos, m_mouseButton);
}


void Q2DTransferFunction::DragInit(INTVECTOR2 vMousePressPos, Qt::MouseButton mouseButton) {
  m_vMousePressPos = vMousePressPos;
  m_mouseButton = mouseButton;

  if (m_eTransferFunctionMode == TFM_EXPERT) {

    if (m_iActiveSwatchIndex >= 0 && m_iActiveSwatchIndex<int(m_pTrans->m_Swatches.size())) {
      TFPolygon& currentSwatch = m_pTrans->m_Swatches[m_iActiveSwatchIndex];

      // left mouse drags points around
      if (mouseButton == Qt::LeftButton) {

        m_bDragging = true;
        m_bDraggingAll = m_eDragMode != DRM_NONE;

        m_iPointSelIndex = -1;
        m_iGradSelIndex = -1;

        FLOATVECTOR2 vfP = Screen2Normalized(m_vMousePressPos);
        // find closest corner point
        float fMinDist = std::numeric_limits<float>::max();
        for (size_t j = 0;j<currentSwatch.pPoints.size();j++) {

          float fDist = sqrt( float(vfP.x-currentSwatch.pPoints[j].x)*float(vfP.x-currentSwatch.pPoints[j].x)
                           +  float(vfP.y-currentSwatch.pPoints[j].y)*float(vfP.y-currentSwatch.pPoints[j].y) );

          if (fMinDist > fDist) {
            fMinDist = fDist;
            m_iPointSelIndex = int(j);
            m_iGradSelIndex = -1;
          }
        }

        // find closest gradient coord
        for (size_t j = 0;j<2;j++) {
          float fDist = sqrt( float(vfP.x-currentSwatch.pGradientCoords[j].x)*float(vfP.x-currentSwatch.pGradientCoords[j].x)
                           +  float(vfP.y-currentSwatch.pGradientCoords[j].y)*float(vfP.y-currentSwatch.pGradientCoords[j].y) );

          if (fMinDist > fDist) {
            fMinDist = fDist;
            m_iPointSelIndex = -1;
            m_iGradSelIndex = int(j);
          }
        }

      }

      // right mouse removes / adds points
      if (mouseButton == Qt::RightButton) {

        FLOATVECTOR2 vfP = Screen2Normalized(m_vMousePressPos);

        // find closest edge and compute the point on that edge
        float fMinDist = std::numeric_limits<float>::max();
        FLOATVECTOR2 vfInserCoord;
        int iInsertIndex = -1;

        for (size_t j = 0;j<currentSwatch.pPoints.size();j++) {
          FLOATVECTOR2 A = currentSwatch.pPoints[j];
          FLOATVECTOR2 B = currentSwatch.pPoints[(j+1)%currentSwatch.pPoints.size()];

          // check if we are deleting a point
          if (currentSwatch.pPoints.size() > 3) {
            INTVECTOR2 vPixelDist = Normalized2Offscreen(vfP)-Normalized2Offscreen(A);
            if ( sqrt( float(vPixelDist.x*vPixelDist.x+vPixelDist.y*vPixelDist.y)) <= m_iSwatchBorderSize*3) {
              currentSwatch.pPoints.erase(currentSwatch.pPoints.begin()+j);
              iInsertIndex = -1;
              emit SwatchChange();
              break;
            }
          }


          FLOATVECTOR2 C = vfP - A;    // Vector from a to Point
          float d = (B - A).length();    // Length of the line segment
          FLOATVECTOR2 V = (B - A)/d;    // Unit Vector from A to B
          float t = V^C;          // Intersection point Distance from A

          float fDist;
          if (t >= 0 && t <= d)
            fDist = (vfP-(A + V*t)).length();
          else
            fDist = std::numeric_limits<float>::max();


          if (fDist < fMinDist) {
            fMinDist = fDist;
            vfInserCoord = vfP;
            iInsertIndex = int(j+1);
          }

        }

        if (iInsertIndex >= 0) {
          currentSwatch.pPoints.insert(currentSwatch.pPoints.begin()+iInsertIndex, vfInserCoord);
          emit SwatchChange();
        }
      }
      update();
    }
  } else {
    m_eSimpleDragMode = SDM_NONE;
    if (mouseButton != Qt::LeftButton) return;
    
    FLOATVECTOR2 vfP = Screen2Normalized(m_vMousePressPos);
    m_iActiveSwatchIndex = PickVertex(vfP, m_iSimpleDragModeSubindex);
    if (m_iActiveSwatchIndex != -1)  {
      m_eSimpleDragMode = SDM_VERTEX;
      m_bDragging = true;
    } else {
      m_iActiveSwatchIndex = PickEdge(vfP, m_iSimpleDragModeSubindex);
      if (m_iActiveSwatchIndex != -1)  {
        m_eSimpleDragMode = SDM_EDGE;
        m_bDragging = true;
      } else {
        m_iActiveSwatchIndex = PickSwatch(vfP);
        if (m_iActiveSwatchIndex != -1)  {
          m_eSimpleDragMode = SDM_POLY;
          m_bDragging = true;
        } else return;
      }
    }

    emit SwatchChange();
    update();
  }
}


bool Q2DTransferFunction::PointInPolygon(const FLOATVECTOR2& point, const TFPolygon& poly) const {
  size_t  i,j=poly.pPoints.size()-1 ;
  bool oddHits=false;

  for (i=0; i<poly.pPoints.size(); i++) {
    if (poly.pPoints[i].y<point.y && poly.pPoints[j].y>=point.y ||
        poly.pPoints[j].y<point.y && poly.pPoints[i].y>=point.y) {
      if (poly.pPoints[i].x+(point.y-poly.pPoints[i].y)/
          (poly.pPoints[j].y-poly.pPoints[i].y)*
          (poly.pPoints[j].x-poly.pPoints[i].x)<point.x) {
        oddHits=!oddHits; 
      }
    }
    j=i;
  }
  return oddHits; 
} 

int Q2DTransferFunction::PickEdge(const FLOATVECTOR2& pickPos, int& iEdgeIndex) const {
  FLOATVECTOR2 pixelPickPos = FLOATVECTOR2(Normalized2Screen(pickPos));
  for (size_t i = 0;i<m_pTrans->m_Swatches.size();i++) {
    if (m_vSimpleSwatchInfo[i].m_eType == PT_OTHER) continue; // skip "other" swatches
    TFPolygon& currentSwatch = m_pTrans->m_Swatches[i];

    for (size_t j = 0;j<currentSwatch.pPoints.size();j++) {
      FLOATVECTOR2 A = FLOATVECTOR2(Normalized2Screen(currentSwatch.pPoints[j]));
      FLOATVECTOR2 B = FLOATVECTOR2(Normalized2Screen(currentSwatch.pPoints[(j+1)%currentSwatch.pPoints.size()]));

      FLOATVECTOR2 C = pixelPickPos - A;  // Vector from a to Point
      float d = (B - A).length();    // Length of the line segment
      FLOATVECTOR2 V = (B - A)/d;    // Unit Vector from A to B
      float t = V^C;                 // Intersection point Distance from A

      float fDist;
      if (t >= 0 && t <= d)
        fDist = (pixelPickPos-(A + V*t)).length();
      else
        fDist = std::numeric_limits<float>::max();

      if (fDist <= max<float>(m_iSwatchBorderSize,4.0f)) {  // give the user at least four pixel to pick
        iEdgeIndex = int(j);
        return int(i);
      }
    }

  }
  return -1;
}

int Q2DTransferFunction::PickVertex(const FLOATVECTOR2& pickPos, int& iVertexIndex) const {
  for (size_t i = 0;i<m_pTrans->m_Swatches.size();i++) {
    if (m_vSimpleSwatchInfo[i].m_eType == PT_OTHER) continue; // skip "other" swatches
    TFPolygon& currentSwatch = m_pTrans->m_Swatches[i];  

    for (size_t j = 0;j<currentSwatch.pPoints.size();j++) {
      FLOATVECTOR2 A = currentSwatch.pPoints[j];      
      INTVECTOR2 vPixelDist = Normalized2Screen(pickPos)-Normalized2Screen(A);
      if ( sqrt( float(vPixelDist.x*vPixelDist.x+vPixelDist.y*vPixelDist.y)) <= m_iSwatchBorderSize*3) {
        iVertexIndex = int(j);
        return int(i);
      }
    }
  }
  return -1;
}

int Q2DTransferFunction::PickSwatch(const FLOATVECTOR2& pickPos) const {
  for (size_t i = 0;i<m_pTrans->m_Swatches.size();i++) {
    TFPolygon& currentSwatch = m_pTrans->m_Swatches[i];  
    if (PointInPolygon(pickPos, currentSwatch)) {
      return int(i);
    }
  }
  return -1;
}

void Q2DTransferFunction::mousePressEvent(QMouseEvent *event) {
  if (m_pTrans == NULL) return;
  // call superclass method
  QWidget::mousePressEvent(event);

  // middle mouse button drags entire view
  if (event->button() == Qt::MidButton) {
    m_vMousePressPos = INTVECTOR2(event->x(), event->y());
    m_eDragMode = DRM_MOVE_ZOOM;
    return;
  }

  SetDragMode( event->modifiers() & Qt::ShiftModifier,
               event->modifiers() & Qt::ControlModifier);

  INTVECTOR2 vMousePressPos = INTVECTOR2(event->x(), event->y());
  DragInit(vMousePressPos, event->button());
}

void Q2DTransferFunction::wheelEvent(QWheelEvent *event) {
  float fZoom = 1.0f-event->delta()/5000.0f;

  FLOATVECTOR2 vNewSize(std::min(1.0f,m_vZoomWindow.z*fZoom), 
                        std::min(1.0f,m_vZoomWindow.w*fZoom));
 
  m_vZoomWindow.x += (m_vZoomWindow.z-vNewSize.x)/2.0f;
  m_vZoomWindow.y += (m_vZoomWindow.w-vNewSize.y)/2.0f;

  m_vZoomWindow.z = vNewSize.x;
  m_vZoomWindow.w = vNewSize.y;

  if (m_vZoomWindow.x + m_vZoomWindow.z > 1.0f) m_vZoomWindow.x = 1.0f-m_vZoomWindow.z;
  if (m_vZoomWindow.y + m_vZoomWindow.w > 1.0f) m_vZoomWindow.y = 1.0f-m_vZoomWindow.w;
  if (m_vZoomWindow.x < 0) m_vZoomWindow.x = 0;
  if (m_vZoomWindow.y < 0) m_vZoomWindow.y = 0;


  m_bBackdropCacheUptodate = false;
  repaint();
}


void Q2DTransferFunction::mouseReleaseEvent(QMouseEvent *event) {
  if (m_pTrans == NULL) return;
  // call superclass method
  QWidget::mouseReleaseEvent(event);

  m_bDragging = false;
  m_bDraggingAll = false;
  m_iPointSelIndex = -1;
  m_iGradSelIndex = -1;
  m_eDragMode = DRM_NONE;
  m_mouseButton = Qt::NoButton;

  update();

  // send message to update the GLtexture
  if( m_eExecutionMode == ONRELEASE ) ApplyFunction();
}

void Q2DTransferFunction::ApplyFunction() {
  // send message to update the GLtexture
  m_MasterController.MemMan()->Changed2DTrans(NULL, m_pTrans);
}


FLOATVECTOR2 Q2DTransferFunction::Rotate(FLOATVECTOR2 point, float angle, FLOATVECTOR2 center, FLOATVECTOR2 rescale) {
  FLOATVECTOR2 temp, newpoint;
  temp = (point - center)*rescale;
  newpoint.x= temp.x*cos(angle)-temp.y*sin(angle);
  newpoint.y= temp.x*sin(angle)+temp.y*cos(angle);
  return (newpoint/rescale)+center;
}

void Q2DTransferFunction::RecomputeLowerPseudoTrisPoints(TFPolygon& currentSwatch, const FLOATVECTOR2& vHandePos) {
  FLOATVECTOR2 A = currentSwatch.pPoints[1];
  FLOATVECTOR2 B = currentSwatch.pPoints[2];

  currentSwatch.pPoints[0].x = vHandePos.x+(A.x - vHandePos.x) * (currentSwatch.pPoints[0].y- vHandePos.y)/(A.y - vHandePos.y);
  currentSwatch.pPoints[3].x = vHandePos.x+(B.x - vHandePos.x) * (currentSwatch.pPoints[3].y- vHandePos.y)/(B.y - vHandePos.y);
}

void Q2DTransferFunction::mouseMoveEvent(QMouseEvent *event) {
  if (m_pTrans == NULL) return;
  // call superclass method
  QWidget::mouseMoveEvent(event);

  if (m_eDragMode == DRM_MOVE_ZOOM) {
    INTVECTOR2 vMouseCurrentPos(event->x(), event->y());

    FLOATVECTOR2 vfPressPos = Screen2Normalized(m_vMousePressPos);
    FLOATVECTOR2 vfCurrentPos = Screen2Normalized(vMouseCurrentPos);

    FLOATVECTOR2 vfDelta = vfPressPos-vfCurrentPos;

    FLOATVECTOR2 vfWinShift;

    if (vfDelta.x < 0) 
      vfWinShift.x = std::max(vfDelta.x, -m_vZoomWindow.x);
    else
      vfWinShift.x = std::min(vfDelta.x, 1.0f-(m_vZoomWindow.x+m_vZoomWindow.z));

    if (vfDelta.y < 0) 
      vfWinShift.y = std::max(vfDelta.y, -m_vZoomWindow.y);
    else
      vfWinShift.y = std::min(vfDelta.y, 1.0f-(m_vZoomWindow.y+m_vZoomWindow.w));

    m_vZoomWindow.x += vfWinShift.x;
    m_vZoomWindow.y += vfWinShift.y;
    m_bBackdropCacheUptodate = false;

    m_vMousePressPos = vMouseCurrentPos;
    update();

    return;
  }

  if (m_bDragging) {

    INTVECTOR2 vMouseCurrentPos(event->x(), event->y());

    FLOATVECTOR2 vfPressPos = Screen2Normalized(m_vMousePressPos);
    FLOATVECTOR2 vfCurrentPos = Screen2Normalized(vMouseCurrentPos);
    FLOATVECTOR2 vfDelta = vfCurrentPos-vfPressPos;

    if (m_eTransferFunctionMode == TFM_EXPERT) {

      TFPolygon& currentSwatch = m_pTrans->m_Swatches[m_iActiveSwatchIndex];

      if (m_bDraggingAll)  {
        switch (m_eDragMode) {
          case DRM_MOVE : {
                    for (unsigned int i= 0;i<currentSwatch.pPoints.size();i++) currentSwatch.pPoints[i] += vfDelta;
                    currentSwatch.pGradientCoords[0] += vfDelta;
                    currentSwatch.pGradientCoords[1] += vfDelta;
                  } break;
          case DRM_ROTATE : {
                    float fScaleFactor = vfDelta.x + vfDelta.y;
                    FLOATVECTOR2 vfCenter(0,0);
                    for (unsigned int i= 0;i<currentSwatch.pPoints.size();i++) vfCenter += currentSwatch.pPoints[i];

                    vfCenter /= currentSwatch.pPoints.size();
                    FLOATVECTOR2 fvRot(cos(fScaleFactor/10),sin(fScaleFactor/10));

                    FLOATVECTOR2 vfRescale(width(),height());
                    vfRescale /= max(width(),height());

                    for (unsigned int i= 0;i<currentSwatch.pPoints.size();i++) currentSwatch.pPoints[i] = Rotate(currentSwatch.pPoints[i], fScaleFactor, vfCenter, vfRescale);
                    currentSwatch.pGradientCoords[0] = Rotate(currentSwatch.pGradientCoords[0], fScaleFactor, vfCenter, vfRescale);
                    currentSwatch.pGradientCoords[1] = Rotate(currentSwatch.pGradientCoords[1], fScaleFactor, vfCenter, vfRescale);


                    } break;
          case DRM_SCALE : {
                    float fScaleFactor = vfDelta.x + vfDelta.y;
                    FLOATVECTOR2 vfCenter(0,0);
                    for (unsigned int i= 0;i<currentSwatch.pPoints.size();i++) vfCenter += currentSwatch.pPoints[i];

                    vfCenter /= currentSwatch.pPoints.size();

                    for (unsigned int i= 0;i<currentSwatch.pPoints.size();i++)
                      currentSwatch.pPoints[i] += (currentSwatch.pPoints[i]-vfCenter)*fScaleFactor;
                    currentSwatch.pGradientCoords[0] += (currentSwatch.pGradientCoords[0]-vfCenter)*fScaleFactor;
                    currentSwatch.pGradientCoords[1] += (currentSwatch.pGradientCoords[1]-vfCenter)*fScaleFactor;

                    } break;
          default : break;
        }
      } else {
        if (m_iPointSelIndex >= 0)  {
          currentSwatch.pPoints[m_iPointSelIndex] += vfDelta;
        } else {
          currentSwatch.pGradientCoords[m_iGradSelIndex] += vfDelta;
        }
      }
    } else {
      if (m_iActiveSwatchIndex < 0) return;
      TFPolygon& currentSwatch = m_pTrans->m_Swatches[m_iActiveSwatchIndex];

      switch (m_eSimpleDragMode) {
        case SDM_POLY : {
          if (m_vSimpleSwatchInfo[m_iActiveSwatchIndex].m_eType == PT_PSEUDOTRIS) vfDelta.y = 0.0;

          for (unsigned int i= 0;i<currentSwatch.pPoints.size();i++) currentSwatch.pPoints[i] += vfDelta;
          currentSwatch.pGradientCoords[0] += vfDelta;
          currentSwatch.pGradientCoords[1] += vfDelta;
          UpdateSwatchType(m_iActiveSwatchIndex);
        } break;
        case SDM_EDGE : {
          if (m_vSimpleSwatchInfo[m_iActiveSwatchIndex].m_eType == PT_RECTANGLE) { 
            switch (m_iSimpleDragModeSubindex) {
              case 0 : currentSwatch.pPoints[0].x += vfDelta.x; 
                       currentSwatch.pPoints[1].x = currentSwatch.pPoints[0].x; 
                       break;
              case 1 : currentSwatch.pPoints[1].y += vfDelta.y; 
                       currentSwatch.pPoints[2].y = currentSwatch.pPoints[1].y; 
                       break;
              case 2 : currentSwatch.pPoints[2].x += vfDelta.x; 
                       currentSwatch.pPoints[3].x = currentSwatch.pPoints[2].x;
                       break;
              case 3 : currentSwatch.pPoints[3].y += vfDelta.y; 
                       currentSwatch.pPoints[0].y = currentSwatch.pPoints[3].y;  
                       break;
            }
            currentSwatch.pGradientCoords[0] = FLOATVECTOR2(currentSwatch.pPoints[1].x, (currentSwatch.pPoints[0].y+currentSwatch.pPoints[1].y)/2.0f);
            currentSwatch.pGradientCoords[1] = FLOATVECTOR2(currentSwatch.pPoints[2].x, (currentSwatch.pPoints[2].y+currentSwatch.pPoints[3].y)/2.0f);
  
          } else {

            switch (m_iSimpleDragModeSubindex) {
              case 1 : currentSwatch.pPoints[1] += vfDelta;
                       currentSwatch.pPoints[2] += vfDelta;
                       break;
              case 3 : currentSwatch.pPoints[0].y += vfDelta.y;
                       currentSwatch.pPoints[3].y += vfDelta.y;
                       break;
            }

            // user dragged the top line under the botom line -> swap lines
            if (currentSwatch.pPoints[1].y > currentSwatch.pPoints[0].y) {
              std::swap(currentSwatch.pPoints[0],currentSwatch.pPoints[1]);
              std::swap(currentSwatch.pPoints[2],currentSwatch.pPoints[3]);
            }

            RecomputeLowerPseudoTrisPoints(currentSwatch, m_vSimpleSwatchInfo[m_iActiveSwatchIndex].m_vHandlePos);
            ComputeGradientForPseudoTris(currentSwatch, currentSwatch.pGradientStops[1].second);
          }
        } break;
        case SDM_VERTEX : {
          if (m_vSimpleSwatchInfo[m_iActiveSwatchIndex].m_eType == PT_RECTANGLE) { 
            currentSwatch.pPoints[m_iSimpleDragModeSubindex] += vfDelta;

            switch (m_iSimpleDragModeSubindex) {
              case 0 : currentSwatch.pPoints[1].x = currentSwatch.pPoints[0].x; 
                       currentSwatch.pPoints[3].y = currentSwatch.pPoints[0].y; 
                       break;
              case 1 : currentSwatch.pPoints[0].x = currentSwatch.pPoints[1].x; 
                       currentSwatch.pPoints[2].y = currentSwatch.pPoints[1].y; 
                       break;
              case 2 : currentSwatch.pPoints[3].x = currentSwatch.pPoints[2].x; 
                       currentSwatch.pPoints[1].y = currentSwatch.pPoints[2].y; 
                       break;
              case 3 : currentSwatch.pPoints[2].x = currentSwatch.pPoints[3].x; 
                       currentSwatch.pPoints[0].y = currentSwatch.pPoints[3].y; 
                       break;
            }
            currentSwatch.pGradientCoords[0] = FLOATVECTOR2(currentSwatch.pPoints[1].x, (currentSwatch.pPoints[0].y+currentSwatch.pPoints[1].y)/2.0f);
            currentSwatch.pGradientCoords[1] = FLOATVECTOR2(currentSwatch.pPoints[2].x, (currentSwatch.pPoints[2].y+currentSwatch.pPoints[3].y)/2.0f);
          } else {
            // must be PT_PSEUDOTRIS

            if (m_iSimpleDragModeSubindex == 1 || m_iSimpleDragModeSubindex == 2){ 
              currentSwatch.pPoints[m_iSimpleDragModeSubindex] += vfDelta;

              switch (m_iSimpleDragModeSubindex) {
                case 1 : currentSwatch.pPoints[2].y = currentSwatch.pPoints[1].y; 
                         break;
                case 2 : currentSwatch.pPoints[1].y = currentSwatch.pPoints[2].y; 
                         break;
              }

              // user dragged the top line under the botom line -> swap lines
              if (currentSwatch.pPoints[1].y > currentSwatch.pPoints[0].y) {
                std::swap(currentSwatch.pPoints[0],currentSwatch.pPoints[1]);
                std::swap(currentSwatch.pPoints[2],currentSwatch.pPoints[3]);
              }

              RecomputeLowerPseudoTrisPoints(currentSwatch, m_vSimpleSwatchInfo[m_iActiveSwatchIndex].m_vHandlePos);
              ComputeGradientForPseudoTris(currentSwatch, currentSwatch.pGradientStops[1].second);
            }
          }
        } break;
      }
    }

    m_vMousePressPos = vMouseCurrentPos;

    update();

    // send message to update the GLtexture
    if( m_eExecutionMode == CONTINUOUS ) ApplyFunction();
  }
}

void Q2DTransferFunction::SetColor(bool bIsEnabled) {
  if (bIsEnabled) {
    m_colorHistogram = QColor(255,255,255);
    m_colorBack = QColor(Qt::black);
    m_colorSwatchBorder = QColor(180, 0, 0);
    m_colorSwatchBorderHighlight = QColor(255, 190, 190);
    m_colorSwatchBorderHighlightCenter = QColor(255, 255, 255);
    m_colorSwatchBorderInactive = QColor(50, 50, 50);
    m_colorSwatchBorderInactiveHighlight = QColor(120, 120, 120);
    m_colorSwatchBorderCircle = QColor(200, 200, 0);
    m_colorSwatchBorderCircleHighlight = QColor(255, 255, 0);
    m_colorSwatchGradCircle = QColor(0, 255, 0);
    m_colorSwatchGradCircleSel = QColor(255, 255, 255);
    m_colorSwatchBorderCircleSel = QColor(255, 255, 255);
  } else {
    m_colorHistogram = QColor(55,55,55);
    m_colorBack = QColor(Qt::black);
    m_colorSwatchBorder = QColor(100, 50, 50);
    m_colorSwatchBorderHighlight = QColor(150, 70, 70);
    m_colorSwatchBorderHighlightCenter = QColor(100, 100, 100);
    m_colorSwatchBorderInactive = QColor(50, 50, 50);
    m_colorSwatchBorderInactiveHighlight = QColor(70, 70, 70);
    m_colorSwatchBorderCircle = QColor(100, 100, 50);
    m_colorSwatchBorderCircleHighlight = QColor(200, 200, 60);
    m_colorSwatchGradCircle = QColor(50, 100, 50);
    m_colorSwatchGradCircleSel = m_colorSwatchGradCircle;
    m_colorSwatchBorderCircleSel = m_colorSwatchBorderCircle;
  }
  m_bHistogramChanged = true;
}

void Q2DTransferFunction::resizeEvent ( QResizeEvent * event ) {
  QTransferFunction::resizeEvent(event);

  m_bBackdropCacheUptodate = false;
}

void Q2DTransferFunction::changeEvent(QEvent * event) {
  // call superclass method
  QWidget::changeEvent(event);

  if (event->type() == QEvent::EnabledChange) {
    SetColor(isEnabled());
    m_bBackdropCacheUptodate = false;
    update();
  }
}

void Q2DTransferFunction::ClearToBlack(QPainter& painter) {
  painter.setPen(Qt::NoPen);
  painter.setBrush(m_colorBack);
  QRect backRect(0,0,painter.viewport().width(),painter.viewport().height());
  painter.drawRect(backRect);}


void Q2DTransferFunction::Draw1DTrans(QPainter& painter) {
  QRectF imageRect(0, 0, 
                  painter.viewport().width(), painter.viewport().height());

  QRectF source(m_vZoomWindow.x * m_pTrans->Get1DTransImage().width(),
                m_vZoomWindow.y * m_pTrans->Get1DTransImage().height(),
                m_vZoomWindow.z * m_pTrans->Get1DTransImage().width(),
                m_vZoomWindow.w * m_pTrans->Get1DTransImage().height());
  painter.drawImage(imageRect,m_pTrans->Get1DTransImage(), source);
}


void Q2DTransferFunction::ComputeCachedImageSize(UINT32 &w , UINT32 &h) const {
  // find an image size that has the same aspect ratio as the histogram
  // but is no smaler than the widget

  w = UINT32(width());
  float fRatio = float(m_pTrans->GetRenderSize().x) / float(m_pTrans->GetRenderSize().y);
  h = w / fRatio;

  if (h > UINT32(height())) {
    h = UINT32(height());
    w = h * fRatio;
  }
}

void Q2DTransferFunction::paintEvent(QPaintEvent *event) {
  // call superclass method
  QWidget::paintEvent(event);

  if (m_pTrans == NULL) {
    QPainter painter(this);
    ClearToBlack(painter);
    return;
  }

  UINT32 w,h;
  ComputeCachedImageSize(w,h);

  // as drawing the histogram can become quite expensive we'll cache it in an image and only redraw if needed
  if (m_bHistogramChanged || !m_bBackdropCacheUptodate || h != m_iCachedHeight || w != m_iCachedWidth) {

    delete m_pBackdropCache;
    m_pBackdropCache = new QPixmap(w,h);

    // attach a painter to the pixmap
    QPainter image_painter(m_pBackdropCache);

    // draw the backdrop into the image
    DrawHistogram(image_painter);
    Draw1DTrans(image_painter);

    // update change detection states
    m_bBackdropCacheUptodate = true;
    m_iCachedHeight = h;
    m_iCachedWidth = w;
  }

  // now draw everything rest into this widget
  QPainter painter;

  QPixmap tmp(w, h);
  painter.begin(&tmp);


  //painter.eraseRect(0,0,w,h);

  // the image captured before (or cached from a previous call)
  painter.drawImage(0,0,m_pBackdropCache->toImage());

  // and the swatches
  DrawSwatches(painter);

  painter.end();
  painter.begin(this);

  QRectF source(0.0, 0.0, w, h);
  QRectF target(0.0, 0.0, width(), height());

  painter.drawImage(target, tmp.toImage(), source);
  DrawSwatcheDecoration(painter);

  painter.end();
}

bool Q2DTransferFunction::LoadFromFile(const QString& strFilename) {
  // hand the load call over to the TransferFunction1D class
  if( m_pTrans->Load(strFilename.toStdString(), m_pTrans->GetSize() ) ) {
    m_iActiveSwatchIndex = 0;
    m_bBackdropCacheUptodate = false;
    update();
    m_MasterController.MemMan()->Changed2DTrans(NULL, m_pTrans);
    emit SwatchChange();
    return true;
  } else return false;
}

bool Q2DTransferFunction::SaveToFile(const QString& strFilename) {
  // hand the save call over to the TransferFunction1D class
  return m_pTrans->Save(strFilename.toStdString());
}


void Q2DTransferFunction::Set1DTrans(const TransferFunction1D* p1DTrans) {
  m_pTrans->Update1DTrans(p1DTrans);
  m_MasterController.MemMan()->Changed2DTrans(NULL, m_pTrans);
  m_bBackdropCacheUptodate = false;
  update();
}

void Q2DTransferFunction::Transfer2DSetActiveSwatch(const int iActiveSwatch) {
  if (iActiveSwatch == -1 && m_pTrans->m_Swatches.size() > 0) return;
  m_iActiveSwatchIndex = iActiveSwatch;
  update();
}

void Q2DTransferFunction::Transfer2DAddCircleSwatch() {
  TFPolygon newSwatch;

  FLOATVECTOR2 vPoint(m_vZoomWindow.x + 0.8f*m_vZoomWindow.z, m_vZoomWindow.y + 0.8f*m_vZoomWindow.w);
  FLOATVECTOR2 vCenter(m_vZoomWindow.x + 0.5f*m_vZoomWindow.z, m_vZoomWindow.y + 0.5f*m_vZoomWindow.w);
  unsigned int iNumberOfSegments = 20;
  for (unsigned int i = 0;i<iNumberOfSegments;i++) {
    newSwatch.pPoints.push_back(vPoint);
    vPoint = Rotate(vPoint, 6.283185f/float(iNumberOfSegments), vCenter, FLOATVECTOR2(1,1));
  }

  newSwatch.pGradientCoords[0] = FLOATVECTOR2(m_vZoomWindow.x + 0.1f*m_vZoomWindow.z, m_vZoomWindow.y + 0.5f*m_vZoomWindow.w);
  newSwatch.pGradientCoords[1] = FLOATVECTOR2(m_vZoomWindow.x + 0.9f*m_vZoomWindow.z, m_vZoomWindow.y + 0.5f*m_vZoomWindow.w);

  GradientStop g1(0,FLOATVECTOR4(0,0,0,0)),g2(0.5f,FLOATVECTOR4(1,1,1,1)),g3(1,FLOATVECTOR4(0,0,0,0));
  newSwatch.pGradientStops.push_back(g1);
  newSwatch.pGradientStops.push_back(g2);
  newSwatch.pGradientStops.push_back(g3);

  m_pTrans->m_Swatches.push_back(newSwatch);

  m_iActiveSwatchIndex = int(m_pTrans->m_Swatches.size()-1);
  m_MasterController.MemMan()->Changed2DTrans(NULL, m_pTrans);
  emit SwatchChange();
}

void Q2DTransferFunction::Transfer2DAddSwatch() {
  TFPolygon newSwatch;

  newSwatch.pPoints.push_back(FLOATVECTOR2(m_vZoomWindow.x + 0.3f*m_vZoomWindow.z, m_vZoomWindow.y + 0.3f*m_vZoomWindow.w));
  newSwatch.pPoints.push_back(FLOATVECTOR2(m_vZoomWindow.x + 0.3f*m_vZoomWindow.z, m_vZoomWindow.y + 0.7f*m_vZoomWindow.w));
  newSwatch.pPoints.push_back(FLOATVECTOR2(m_vZoomWindow.x + 0.7f*m_vZoomWindow.z, m_vZoomWindow.y + 0.7f*m_vZoomWindow.w));
  newSwatch.pPoints.push_back(FLOATVECTOR2(m_vZoomWindow.x + 0.7f*m_vZoomWindow.z, m_vZoomWindow.y + 0.3f*m_vZoomWindow.w));

  newSwatch.pGradientCoords[0] = FLOATVECTOR2(m_vZoomWindow.x + 0.3f*m_vZoomWindow.z, m_vZoomWindow.y + 0.5f*m_vZoomWindow.w);
  newSwatch.pGradientCoords[1] = FLOATVECTOR2(m_vZoomWindow.x + 0.7f*m_vZoomWindow.z, m_vZoomWindow.y + 0.5f*m_vZoomWindow.w);

  GradientStop g1(0,FLOATVECTOR4(0,0,0,0)),g2(0.5f,FLOATVECTOR4(1,1,1,1)),g3(1,FLOATVECTOR4(0,0,0,0));
  newSwatch.pGradientStops.push_back(g1);
  newSwatch.pGradientStops.push_back(g2);
  newSwatch.pGradientStops.push_back(g3);

  m_pTrans->m_Swatches.push_back(newSwatch);

  m_iActiveSwatchIndex = int(m_pTrans->m_Swatches.size()-1);
  m_MasterController.MemMan()->Changed2DTrans(NULL, m_pTrans);
  emit SwatchChange();
}

void Q2DTransferFunction::Transfer2DAddRectangleSwatch() {
  TFPolygon newSwatch;

  newSwatch.pPoints.push_back(FLOATVECTOR2(m_vZoomWindow.x + 0.3f*m_vZoomWindow.z, m_vZoomWindow.y + 0.3f*m_vZoomWindow.w));
  newSwatch.pPoints.push_back(FLOATVECTOR2(m_vZoomWindow.x + 0.3f*m_vZoomWindow.z, m_vZoomWindow.y + 0.7f*m_vZoomWindow.w));
  newSwatch.pPoints.push_back(FLOATVECTOR2(m_vZoomWindow.x + 0.7f*m_vZoomWindow.z, m_vZoomWindow.y + 0.7f*m_vZoomWindow.w));
  newSwatch.pPoints.push_back(FLOATVECTOR2(m_vZoomWindow.x + 0.7f*m_vZoomWindow.z, m_vZoomWindow.y + 0.3f*m_vZoomWindow.w));

  newSwatch.pGradientCoords[0] = FLOATVECTOR2(m_vZoomWindow.x + 0.3f*m_vZoomWindow.z, m_vZoomWindow.y + 0.5f*m_vZoomWindow.w);
  newSwatch.pGradientCoords[1] = FLOATVECTOR2(m_vZoomWindow.x + 0.7f*m_vZoomWindow.z, m_vZoomWindow.y + 0.5f*m_vZoomWindow.w);

  GradientStop g1(0,FLOATVECTOR4(0,0,0,0)),g2(0.5,FLOATVECTOR4(rand()/float(RAND_MAX),rand()/float(RAND_MAX),rand()/float(RAND_MAX),1)),g3(1,FLOATVECTOR4(0,0,0,0));
  newSwatch.pGradientStops.push_back(g1);
  newSwatch.pGradientStops.push_back(g2);
  newSwatch.pGradientStops.push_back(g3);

  m_pTrans->m_Swatches.push_back(newSwatch);

  UpdateSwatchTypes();

  m_iActiveSwatchIndex = int(m_pTrans->m_Swatches.size()-1);
  m_MasterController.MemMan()->Changed2DTrans(NULL, m_pTrans);
  emit SwatchChange();
}


void Q2DTransferFunction::ComputeGradientForPseudoTris(TFPolygon& swatch, const FLOATVECTOR4& color) {
  GradientStop g1(0,FLOATVECTOR4(0,0,0,0)),
               g2(0.5f,color),
               g3(1.0f,FLOATVECTOR4(0,0,0,0));
  swatch.pGradientStops.clear();
  swatch.pGradientStops.push_back(g1);
  swatch.pGradientStops.push_back(g2);
  swatch.pGradientStops.push_back(g3);

  FLOATVECTOR2 persCorrection(m_iCachedWidth,m_iCachedHeight);
  persCorrection /= persCorrection.maxVal();


  FLOATVECTOR2 vTop       = (swatch.pPoints[1]+swatch.pPoints[2])*persCorrection/2.0f;
  FLOATVECTOR2 vBottom    = (swatch.pPoints[3]+swatch.pPoints[0])*persCorrection/2.0f;
  FLOATVECTOR2 vDirection = (vBottom-vTop);
  FLOATVECTOR2 vPerpendicular(vDirection.y,-vDirection.x);
  vPerpendicular.normalize();
  vPerpendicular *= (swatch.pPoints[1]-swatch.pPoints[2]).length()/2.0f;

  swatch.pGradientCoords[0] = (vTop-vPerpendicular)/persCorrection;
  swatch.pGradientCoords[1] = (vTop+vPerpendicular)/persCorrection;
}

void Q2DTransferFunction::Transfer2DAddPseudoTrisSwatch() {
  TFPolygon newSwatch;

  FLOATVECTOR2 p1 = FLOATVECTOR2(m_vZoomWindow.x + 0.3f*m_vZoomWindow.z, m_vZoomWindow.y + 0.3f*m_vZoomWindow.w);
  FLOATVECTOR2 p2 = FLOATVECTOR2(m_vZoomWindow.x + 0.7f*m_vZoomWindow.z, m_vZoomWindow.y + 0.3f*m_vZoomWindow.w);

  FLOATVECTOR2 handlePoint = FLOATVECTOR2((p2.x+p1.x)/2.0f,1.0f);

  FLOATVECTOR2 p0 = (p1+handlePoint)/2.0f;
  FLOATVECTOR2 p3 = (p2+handlePoint)/2.0f;

  newSwatch.pPoints.push_back(p0);
  newSwatch.pPoints.push_back(p1);
  newSwatch.pPoints.push_back(p2);
  newSwatch.pPoints.push_back(p3);
  ComputeGradientForPseudoTris(newSwatch, FLOATVECTOR4(rand()/float(RAND_MAX),rand()/float(RAND_MAX),rand()/float(RAND_MAX),1));

  m_pTrans->m_Swatches.push_back(newSwatch);
  UpdateSwatchTypes();

  m_iActiveSwatchIndex = int(m_pTrans->m_Swatches.size()-1);
  m_MasterController.MemMan()->Changed2DTrans(NULL, m_pTrans);
  emit SwatchChange();
}


void Q2DTransferFunction::Transfer2DDeleteSwatch(){
  if (m_iActiveSwatchIndex != -1) {
    m_pTrans->m_Swatches.erase(m_pTrans->m_Swatches.begin()+m_iActiveSwatchIndex);

    m_iActiveSwatchIndex = min<int>(m_iActiveSwatchIndex, int(m_pTrans->m_Swatches.size()-1));
    m_MasterController.MemMan()->Changed2DTrans(NULL, m_pTrans);
    emit SwatchChange();
  }
}

void Q2DTransferFunction::Transfer2DUpSwatch(){
  if (m_iActiveSwatchIndex > 0) {
    TFPolygon tmp = m_pTrans->m_Swatches[m_iActiveSwatchIndex-1];
    m_pTrans->m_Swatches[m_iActiveSwatchIndex-1] = m_pTrans->m_Swatches[m_iActiveSwatchIndex];
    m_pTrans->m_Swatches[m_iActiveSwatchIndex] = tmp;

    m_iActiveSwatchIndex--;
    m_MasterController.MemMan()->Changed2DTrans(NULL, m_pTrans);
    emit SwatchChange();
  }
}

void Q2DTransferFunction::Transfer2DDownSwatch(){
  if (m_iActiveSwatchIndex >= 0 && m_iActiveSwatchIndex < int(m_pTrans->m_Swatches.size()-1)) {
    TFPolygon tmp = m_pTrans->m_Swatches[m_iActiveSwatchIndex+1];
    m_pTrans->m_Swatches[m_iActiveSwatchIndex+1] = m_pTrans->m_Swatches[m_iActiveSwatchIndex];
    m_pTrans->m_Swatches[m_iActiveSwatchIndex] = tmp;

    m_iActiveSwatchIndex++;
    m_MasterController.MemMan()->Changed2DTrans(NULL, m_pTrans);
    emit SwatchChange();
  }
}

void Q2DTransferFunction::SetActiveGradientType(bool bRadial) {
  if(static_cast<size_t>(m_iActiveSwatchIndex) <
     m_pTrans->m_Swatches.size()) {
       if (m_pTrans->m_Swatches[m_iActiveSwatchIndex].bRadial != bRadial) {
          m_pTrans->m_Swatches[m_iActiveSwatchIndex].bRadial = bRadial;
          m_MasterController.MemMan()->Changed2DTrans(NULL, m_pTrans);
          update();
       }
  }
}

void Q2DTransferFunction::AddGradient(GradientStop stop) {
  for (std::vector< GradientStop >::iterator i = m_pTrans->m_Swatches[m_iActiveSwatchIndex].pGradientStops.begin();i<m_pTrans->m_Swatches[m_iActiveSwatchIndex].pGradientStops.end();i++) {
    if (i->first > stop.first) {
      m_pTrans->m_Swatches[m_iActiveSwatchIndex].pGradientStops.insert(i, stop);
      return;
    }
  }
  m_pTrans->m_Swatches[m_iActiveSwatchIndex].pGradientStops.push_back(stop);
  m_MasterController.MemMan()->Changed2DTrans(NULL, m_pTrans);
  update();
}

void Q2DTransferFunction::DeleteGradient(unsigned int i) {
  m_pTrans->m_Swatches[m_iActiveSwatchIndex].pGradientStops.erase(m_pTrans->m_Swatches[m_iActiveSwatchIndex].pGradientStops.begin()+i);
  m_MasterController.MemMan()->Changed2DTrans(NULL, m_pTrans);
  update();
}

void Q2DTransferFunction::SetGradient(unsigned int i, GradientStop stop) {
  m_pTrans->m_Swatches[m_iActiveSwatchIndex].pGradientStops[i] = stop;
  m_MasterController.MemMan()->Changed2DTrans(NULL, m_pTrans);
  update();
}


void Q2DTransferFunction::UpdateSwatchType(size_t i) {
  TFPolygon& currentSwatch = m_pTrans->m_Swatches[i];
  FLOATVECTOR2 vHandle;
  E2DSimpleModePolyType polyType = ClassifySwatch(currentSwatch,vHandle);
  m_vSimpleSwatchInfo[i] = SimpleSwatchInfo(polyType,vHandle);
}


void Q2DTransferFunction::UpdateSwatchTypes() {
  m_vSimpleSwatchInfo.clear();
  m_vSimpleSwatchInfo.resize(m_pTrans->m_Swatches.size());
  for (size_t i = 0;i<m_pTrans->m_Swatches.size();i++) {
    UpdateSwatchType(i);
  }
}

void Q2DTransferFunction::Toggle2DTFMode() { 
  Set2DTFMode(E2DTransferFunctionMode((int(m_eTransferFunctionMode)+1)%int(TFM_INVALID))); 
}

void Q2DTransferFunction::Set2DTFMode(E2DTransferFunctionMode TransferFunctionMode) { 
  m_eTransferFunctionMode = TransferFunctionMode; 
  if (m_eTransferFunctionMode == TFM_BASIC) UpdateSwatchTypes();
  update();
}
