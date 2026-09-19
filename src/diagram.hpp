// diagram.hpp -- shapes and flow diagrams: the Draw Partner end of charts.
//
// A shapes block is free drawing on a 12 x 12 grid over the block, the same
// grid "at" uses for the slide: rectangles, ellipses, polygons, lines and
// arrows, labels.  A flow block is boxes and arrows laid out for you: the
// agent names the steps and the links, never a coordinate.
#pragma once

#include <string>
#include <vector>

#include "scene.hpp"

namespace ch {

struct Shape {
  enum Kind { RECT, ELLIPSE, POLY, LINE, LABEL };
  Kind kind = RECT;
  std::vector<Pt> pts; // grid units.  RECT/ELLIPSE: two corners; POLY/LINE: the vertices; LABEL: the anchor
  std::string text;
  int color = -1;      // fill of a closed shape, ink of a line or label; -1 = the theme's
  int text_color = -1;
  int border = -1;     // outline of a closed shape; -1 = the theme's frame on a plain shape, none on a coloured one
  int dither = 0;      // 1..3 quarters of the background mixed into the fill
  int depth = 0;       // 3-D extrusion, as bars have
  int width = 1;       // line width, 1..4
  int size = 1;        // text height in cells, 1..3
  int align = -1;      // LABEL: the anchor is the text's top left (-1), top centre (0) or top right (1)
  bool shadow = false; // the DOS drop shadow
  bool fill = true;
  bool dash = false;
  bool head = false, tail = false; // arrowheads on the last and the first point
};

struct FlowNode {
  std::string id, text;
  int color = -1;
};

struct FlowEdge {
  int from = 0, to = 0; // into Flow::nodes
  std::string text;
  int color = -1;
  bool dash = false;
};

struct Flow {
  std::vector<FlowNode> nodes;
  std::vector<FlowEdge> edges;
  bool down = false; // "dir": "down" stacks the steps; the default runs them left to right
};

// Both draw into r over a background of colour bg (a panel, or the slide) and
// leave --check a note on the scene for anything that did not fit.
void draw_shapes(Scene &sc, Rect r, const std::vector<Shape> &shapes, uint8_t bg);
void draw_flow(Scene &sc, Rect r, const Flow &f, uint8_t bg);

} // namespace ch
