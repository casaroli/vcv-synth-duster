#include "plugin.hpp"

#include <cmath>
#include <vector>
#include <settings.hpp>

// Set to 1 to overlay obstacle rects (cyan), cable segments (green tubes),
// and a red segment-count bar — useful when debugging collision geometry.
#define DUSTER_DEBUG_OBSTACLES 0


struct Duster : Module {
	enum ParamId {
		MODE_PARAM,
		NUM_PARAMS
	};

	Duster() {
		config(NUM_PARAMS, 0, 0, 0);
		configSwitch(MODE_PARAM, 0.f, 1.f, 0.f, "View", {"Side", "Top"});
	}
};


struct BristleDef {
	float anchorX, anchorY;   // base on handle, panel-local mm
	float restX, restY;       // rest tip position, panel-local mm
	float width;              // stroke width in mm
	float r, g, b, a;         // colour
};

#include "Bristles.inl"


struct Bristle {
	float dispX = 0.f;
	float dispY = 0.f;
	float velX = 0.f;
	float velY = 0.f;
};


struct SegmentObstacle {
	math::Vec a;
	math::Vec b;
	float radius;
};


// A point on the handle's perimeter in top-down mode: anchor position plus
// outward unit normal. The anchor is inset slightly inward so the bristle
// base stays hidden behind the handle when undisplaced.
struct PerimPoint {
	float x, y;
	float nx, ny;
};


// Procedural bristle layer: draws each bristle as an independent line with
// its own (displacement, velocity) state and reacts to nearby ParamWidgets,
// PortWidgets, and cables as physical obstacles.
struct BristleWidget : widget::Widget {
	static constexpr float kStiffness = 0.18f;
	static constexpr float kDamping = 0.22f;
	static constexpr float kDragGainX = 1.6f;
	static constexpr float kDragGainY = 0.6f;
	static constexpr float kContactDamping = 0.55f;
	static constexpr float kPushEpsilon = 0.1f;     // rack-px past the obstacle edge
	static constexpr float kMaxDispMm = 8.0f;

	// Cable obstacles: sample each cable's sagging curve into N segments.
	// Matches VCV Rack's own quadratic-bezier slump formula in
	// CableWidget.cpp: control point at midpoint, displaced vertically by
	// (1 - cableTension) * (kCableSagBaseline + kCableSagPerPx * distance).
	static constexpr int   kCableSamples = 24;
	static constexpr float kCableRadiusPx = 3.0f;
	static constexpr float kCableSagBaseline = 150.0f;
	static constexpr float kCableSagPerPx = 1.0f;

	// Top mode: bristles are distributed around the *perimeter* of the
	// handle (1.6, 4)-(39.04, 108). At rest each one's projection is a
	// point inset inside the wood — invisible behind the panel. Any push
	// stretches the bristle out along the edge's outward normal, with a
	// small tangential sway for the lateral component of the push.
	static constexpr float kTopHandleX0 = 5.0f;
	static constexpr float kTopHandleX1 = 35.64f;
	static constexpr float kTopHandleY0 = 4.0f;
	static constexpr float kTopHandleY1 = 108.0f;
	static constexpr float kTopAnchorInset = 1.5f;   // mm inward from edge
	static constexpr float kTopPeekScale = 12.0f;    // mm of peek per mm of disp
	static constexpr float kTopPeekLatScale = 0.6f;  // tangential sway scale
	// Softer spring in top mode so the rare contacts on the top/side edges
	// still accumulate enough displacement to read as stretched bristles.
	static constexpr float kTopStiffness = 0.06f;

	static PerimPoint computePerimeter(int idx, int total) {
		const float topL = kTopHandleX1 - kTopHandleX0;
		const float rightL = kTopHandleY1 - kTopHandleY0;
		const float bottomL = topL;
		const float leftL = rightL;
		const float perim = topL + rightL + bottomL + leftL;
		float s = ((float) idx / (float) total) * perim;

		PerimPoint p;
		if (s < topL) {
			p.x = kTopHandleX0 + s;
			p.y = kTopHandleY0;
			p.nx = 0.f; p.ny = -1.f;
		}
		else if (s < topL + rightL) {
			p.x = kTopHandleX1;
			p.y = kTopHandleY0 + (s - topL);
			p.nx = 1.f; p.ny = 0.f;
		}
		else if (s < topL + rightL + bottomL) {
			p.x = kTopHandleX1 - (s - topL - rightL);
			p.y = kTopHandleY1;
			p.nx = 0.f; p.ny = 1.f;
		}
		else {
			p.x = kTopHandleX0;
			p.y = kTopHandleY1 - (s - topL - rightL - bottomL);
			p.nx = -1.f; p.ny = 0.f;
		}
		// Inset the anchor inward so the base sits inside the handle.
		p.x -= p.nx * kTopAnchorInset;
		p.y -= p.ny * kTopAnchorInset;
		return p;
	}

	Bristle bristles[kNumBristles];

	// Parent DusterWidget; we read its box.pos as the brush position and skip
	// it when collecting obstacles.
	widget::Widget* brush = nullptr;
	// Source of the view-mode param (0 = side, 1 = top).
	Duster* dusterModule = nullptr;
	math::Vec lastBrushPos;
	bool firstStep = true;

	std::vector<math::Rect> rectObstacles;
	std::vector<SegmentObstacle> segObstacles;

	void setBrush(widget::Widget* b) {
		brush = b;
	}

	void setDusterModule(Duster* d) {
		dusterModule = d;
	}

	bool isTopMode() const {
		return dusterModule
			&& dusterModule->params[Duster::MODE_PARAM].getValue() > 0.5f;
	}

	void collectObstacles() {
		rectObstacles.clear();
		segObstacles.clear();
		app::RackWidget* rack = APP->scene->rack;
		if (!rack)
			return;

		for (app::ModuleWidget* mw : rack->getModules()) {
			if (mw == brush)
				continue;
			math::Vec mwPos = mw->box.pos;
			for (app::ParamWidget* pw : mw->getParams()) {
				rectObstacles.emplace_back(mwPos.plus(pw->box.pos), pw->box.size);
			}
			for (app::PortWidget* port : mw->getPorts()) {
				rectObstacles.emplace_back(mwPos.plus(port->box.pos), port->box.size);
			}
		}

		// Cables: approximate each as a quadratic bezier from input to output
		// with a downward-sagging control point, sampled into line segments.
		// Endpoints must be in moduleContainer-local coords — the same space
		// the rect obstacles (mw.box.pos.plus(pw.box.pos)) and the bristle
		// tips (brush.box.pos + local mm) live in. Walking up to the rack
		// instead would add an unwanted moduleContainer.box.pos offset.
		// Iterate getCables() (not getCompleteCables) and filter ourselves —
		// in case completeness has stricter semantics than we expect.
		widget::Widget* mc = rack->getModuleContainer();
		for (app::CableWidget* cw : rack->getCables()) {
			if (!cw || !cw->inputPort || !cw->outputPort)
				continue;
			math::Vec p0 = cw->outputPort->getRelativeOffset(
				cw->outputPort->box.size.div(2.f), mc);
			math::Vec p2 = cw->inputPort->getRelativeOffset(
				cw->inputPort->box.size.div(2.f), mc);
			float dist = p2.minus(p0).norm();
			float sag = (1.f - settings::cableTension)
				* (kCableSagBaseline + kCableSagPerPx * dist);
			math::Vec p1 = p0.plus(p2).mult(0.5f).plus(math::Vec(0.f, sag));
			math::Vec prev = p0;
			for (int i = 1; i <= kCableSamples; ++i) {
				float t = (float) i / kCableSamples;
				float u = 1.f - t;
				math::Vec p = p0.mult(u * u)
					.plus(p1.mult(2.f * u * t))
					.plus(p2.mult(t * t));
				segObstacles.push_back({prev, p, kCableRadiusPx});
				prev = p;
			}
		}
	}

	void step() override {
		widget::Widget::step();
		if (!brush)
			return;

		if (firstStep) {
			lastBrushPos = brush->box.pos;
			firstStep = false;
			return;
		}

		math::Vec brushVel = brush->box.pos.minus(lastBrushPos);
		lastBrushPos = brush->box.pos;

		collectObstacles();

		const float mm = mm2px(1.0f);
		const math::Vec brushOrigin = brush->box.pos;

		// Brush velocity in mm/frame so it shares units with displacement.
		float velMmX = brushVel.x / mm;
		float velMmY = brushVel.y / mm;

		bool topMode = isTopMode();

		for (int i = 0; i < kNumBristles; ++i) {
			const BristleDef& d = kBristles[i];
			Bristle& b = bristles[i];

			// Drag target: tips lag opposite to handle motion. In top mode
			// there's no surface friction — bristles only react to obstacle
			// pushes, so the drag target is zero.
			float tX = topMode ? 0.f : -velMmX * kDragGainX;
			float tY = topMode ? 0.f : -velMmY * kDragGainY;

			// Damped spring toward target. Softer in top mode (see comment
			// near kTopStiffness) so weak contacts visibly stretch bristles.
			float stiff = topMode ? kTopStiffness : kStiffness;
			float fX = (tX - b.dispX) * stiff - b.velX * kDamping;
			float fY = (tY - b.dispY) * stiff - b.velY * kDamping;
			b.velX += fX;
			b.velY += fY;
			b.dispX += b.velX;
			b.dispY += b.velY;

			// Anchor (fixed) and tip (moving) in rack pixels — same space as
			// the obstacle rects and segments. In top mode the bristle
			// projects to a single dot, so anchor and rest tip share an
			// (x, y) and the displacement is just the deflection.
			float anchorPxX, anchorPxY, tipPxX, tipPxY;
			if (topMode) {
				PerimPoint pp = computePerimeter(i, kNumBristles);
				anchorPxX = brushOrigin.x + pp.x * mm;
				anchorPxY = brushOrigin.y + pp.y * mm;
				tipPxX = brushOrigin.x + (pp.x + b.dispX) * mm;
				tipPxY = brushOrigin.y + (pp.y + b.dispY) * mm;
			}
			else {
				anchorPxX = brushOrigin.x + d.anchorX * mm;
				anchorPxY = brushOrigin.y + d.anchorY * mm;
				tipPxX = brushOrigin.x + (d.restX + b.dispX) * mm;
				tipPxY = brushOrigin.y + (d.restY + b.dispY) * mm;
			}

			// Sample multiple points along the bristle body so small
			// obstacles touching it anywhere — not just at the tip — push
			// the tip aside. Each sample's resolved push gets transferred
			// to the tip displacement; the line bends, the spring resolves
			// the rest over the next frames.
			constexpr int kBodySamples = 4;
			for (int si = 1; si <= kBodySamples; ++si) {
				float t = (float) si / kBodySamples;
				float sX = anchorPxX + (tipPxX - anchorPxX) * t;
				float sY = anchorPxY + (tipPxY - anchorPxY) * t;

				// Rectangular obstacles (knobs, jacks).
				for (const math::Rect& r : rectObstacles) {
					if (sX < r.pos.x || sX > r.pos.x + r.size.x)
						continue;
					if (sY < r.pos.y || sY > r.pos.y + r.size.y)
						continue;

					float leftPen = sX - r.pos.x;
					float rightPen = r.pos.x + r.size.x - sX;
					float topPen = sY - r.pos.y;
					float bottomPen = r.pos.y + r.size.y - sY;

					int axis = 0;
					float minPen = leftPen;
					if (rightPen < minPen)  { minPen = rightPen;  axis = 1; }
					if (topPen < minPen)    { minPen = topPen;    axis = 2; }
					if (bottomPen < minPen) { minPen = bottomPen; axis = 3; }

					float pushPxX = 0.f, pushPxY = 0.f;
					switch (axis) {
						case 0: pushPxX = -(leftPen   + kPushEpsilon); break;
						case 1: pushPxX = +(rightPen  + kPushEpsilon); break;
						case 2: pushPxY = -(topPen    + kPushEpsilon); break;
						case 3: pushPxY = +(bottomPen + kPushEpsilon); break;
					}
					b.dispX += pushPxX / mm;
					b.dispY += pushPxY / mm;
					tipPxX += pushPxX;
					tipPxY += pushPxY;
					sX += pushPxX;
					sY += pushPxY;

					b.velX *= kContactDamping;
					b.velY *= kContactDamping;
				}

				// Segment obstacles (cables).
				for (const SegmentObstacle& seg : segObstacles) {
					float abx = seg.b.x - seg.a.x;
					float aby = seg.b.y - seg.a.y;
					float ab2 = abx * abx + aby * aby;
					if (ab2 < 1e-6f)
						continue;
					float apx = sX - seg.a.x;
					float apy = sY - seg.a.y;
					float tt = (apx * abx + apy * aby) / ab2;
					if (tt < 0.f) tt = 0.f;
					if (tt > 1.f) tt = 1.f;
					float cx = seg.a.x + abx * tt;
					float cy = seg.a.y + aby * tt;
					float dxs = sX - cx;
					float dys = sY - cy;
					float d2 = dxs * dxs + dys * dys;
					if (d2 >= seg.radius * seg.radius)
						continue;
					float d = std::sqrt(d2);
					float nx, ny;
					if (d > 1e-6f) {
						nx = dxs / d;
						ny = dys / d;
					}
					else {
						nx = 0.f;
						ny = -1.f;
					}
					float push = (seg.radius - d) + kPushEpsilon;
					float pushPxX = nx * push;
					float pushPxY = ny * push;
					b.dispX += pushPxX / mm;
					b.dispY += pushPxY / mm;
					tipPxX += pushPxX;
					tipPxY += pushPxY;
					sX += pushPxX;
					sY += pushPxY;

					b.velX *= kContactDamping;
					b.velY *= kContactDamping;
				}
			}

			if (b.dispX >  kMaxDispMm) b.dispX =  kMaxDispMm;
			if (b.dispX < -kMaxDispMm) b.dispX = -kMaxDispMm;
			if (b.dispY >  kMaxDispMm) b.dispY =  kMaxDispMm;
			if (b.dispY < -kMaxDispMm) b.dispY = -kMaxDispMm;
		}
	}

	void draw(const DrawArgs& args) override {
		const float mm = mm2px(1.0f);

#if DUSTER_DEBUG_OBSTACLES
		// Count indicator: a red bar in the top-left whose width grows with
		// the number of cable segments collected this frame. If this bar
		// stays empty, collectObstacles never finds a cable at all.
		nvgBeginPath(args.vg);
		nvgRect(args.vg, 2.f, 2.f, (float) segObstacles.size() * 2.f, 4.f);
		nvgFillColor(args.vg, nvgRGBAf(1.f, 0.f, 0.f, 0.8f));
		nvgFill(args.vg);

		if (brush) {
			// Cyan boxes over each knob/jack rect obstacle.
			for (const math::Rect& r : rectObstacles) {
				nvgBeginPath(args.vg);
				nvgRect(args.vg,
					r.pos.x - brush->box.pos.x,
					r.pos.y - brush->box.pos.y,
					r.size.x, r.size.y);
				nvgStrokeColor(args.vg, nvgRGBAf(0.f, 0.9f, 1.f, 0.7f));
				nvgFillColor(args.vg, nvgRGBAf(0.f, 0.9f, 1.f, 0.18f));
				nvgStrokeWidth(args.vg, 1.f);
				nvgFill(args.vg);
				nvgStroke(args.vg);
			}

			// Green tubes laid over each cable segment.
			for (const SegmentObstacle& s : segObstacles) {
				nvgBeginPath(args.vg);
				nvgMoveTo(args.vg,
					s.a.x - brush->box.pos.x, s.a.y - brush->box.pos.y);
				nvgLineTo(args.vg,
					s.b.x - brush->box.pos.x, s.b.y - brush->box.pos.y);
				nvgStrokeColor(args.vg, nvgRGBAf(0.f, 1.f, 0.f, 0.6f));
				nvgStrokeWidth(args.vg, s.radius * 2.f);
				nvgLineCap(args.vg, NVG_ROUND);
				nvgStroke(args.vg);
			}
		}
#endif

		bool topMode = isTopMode();

		for (int i = 0; i < kNumBristles; ++i) {
			const BristleDef& d = kBristles[i];
			const Bristle& b = bristles[i];

			float ax, ay, tx, ty;
			if (topMode) {
				// Perimeter peek: anchor inside the wood, tip extends out
				// along the edge's outward normal by |disp| * scale, plus a
				// tangential sway from the displacement's lateral component.
				PerimPoint pp = computePerimeter(i, kNumBristles);
				float dispMag = std::sqrt(b.dispX * b.dispX + b.dispY * b.dispY);
				float tanX = -pp.ny;
				float tanY = pp.nx;
				float sway = b.dispX * tanX + b.dispY * tanY;

				ax = pp.x * mm;
				ay = pp.y * mm;
				tx = (pp.x + pp.nx * dispMag * kTopPeekScale
					+ tanX * sway * kTopPeekLatScale) * mm;
				ty = (pp.y + pp.ny * dispMag * kTopPeekScale
					+ tanY * sway * kTopPeekLatScale) * mm;
			}
			else {
				ax = d.anchorX * mm;
				ay = d.anchorY * mm;
				tx = (d.restX + b.dispX) * mm;
				ty = (d.restY + b.dispY) * mm;
			}

			nvgBeginPath(args.vg);
			nvgMoveTo(args.vg, ax, ay);
			nvgLineTo(args.vg, tx, ty);
			nvgStrokeWidth(args.vg, d.width * mm);
			nvgStrokeColor(args.vg, nvgRGBAf(d.r, d.g, d.b, d.a));
			nvgLineCap(args.vg, NVG_ROUND);
			nvgStroke(args.vg);
		}
	}
};


struct DusterWidget : ModuleWidget {
	BristleWidget* bristleLayer = nullptr;
	app::SvgPanel* panel = nullptr;
	std::shared_ptr<window::Svg> sideSvg;
	std::shared_ptr<window::Svg> topSvg;
	int currentMode = -1;
	bool didDrag = false;

	DusterWidget(Duster* module) {
		setModule(module);

		sideSvg = window::Svg::load(
			asset::plugin(pluginInstance, "res/Duster_handle.svg"));
		topSvg = window::Svg::load(
			asset::plugin(pluginInstance, "res/Duster_handle_top.svg"));
		panel = createPanel<app::SvgPanel>(
			asset::plugin(pluginInstance, "res/Duster_handle.svg"));
		setPanel(panel);

		bristleLayer = new BristleWidget();
		bristleLayer->box.size = math::Vec(mm2px(40.64f), mm2px(128.5f));
		bristleLayer->setBrush(this);
		bristleLayer->setDusterModule(module);
		addChild(bristleLayer);

		// Force the bristle layer to the front of the children list — i.e.,
		// drawn FIRST (back-most). setPanel inserts the panel near the
		// front, so without this the bristles end up rendering on top of
		// the wood instead of underneath it.
		children.remove(bristleLayer);
		children.push_front(bristleLayer);

		// View-mode switch: small slide on the right side of the handle,
		// at a y that's on the wood in both panels.
		addParam(createParamCentered<CKSS>(
			mm2px(math::Vec(33.f, 95.f)), module, Duster::MODE_PARAM));
	}

	void step() override {
		ModuleWidget::step();
		if (module) {
			int newMode = (int) module->params[Duster::MODE_PARAM].getValue();
			if (newMode != currentMode) {
				panel->setBackground(newMode == 1 ? topSvg : sideSvg);
				currentMode = newMode;
			}
		}
	}

	// Free movement: ignore the rack's snap-to-grid + push-others for left
	// drags. Right/middle still chain so the context menu keeps working.

	void onDragStart(const DragStartEvent& e) override {
		if (e.button != GLFW_MOUSE_BUTTON_LEFT) {
			ModuleWidget::onDragStart(e);
			return;
		}
		didDrag = false;
	}

	void onDragMove(const DragMoveEvent& e) override {
		if (e.button != GLFW_MOUSE_BUTTON_LEFT) {
			ModuleWidget::onDragMove(e);
			return;
		}
		float zoom = getAbsoluteZoom();
		if (zoom > 0.f) {
			box.pos = box.pos.plus(e.mouseDelta.div(zoom));
			didDrag = true;
		}
	}

	void onDragEnd(const DragEndEvent& e) override {
		if (e.button != GLFW_MOUSE_BUTTON_LEFT) {
			ModuleWidget::onDragEnd(e);
			return;
		}
		if (!didDrag)
			return;

		// Snap to the nearest rail-grid intersection if the release is close
		// enough on both axes. setModulePosForce pushes neighbors aside.
		const float gw = RACK_GRID_WIDTH;
		const float gh = RACK_GRID_HEIGHT;
		const float thresholdX = gw;          // within 1 HP
		const float thresholdY = gh * 0.25f;  // within ~1/4 row

		float sx = std::round(box.pos.x / gw) * gw;
		float sy = std::round(box.pos.y / gh) * gh;
		if (std::abs(box.pos.x - sx) <= thresholdX
			&& std::abs(box.pos.y - sy) <= thresholdY) {
			if (APP->scene->rack) {
				APP->scene->rack->setModulePosForce(this, math::Vec(sx, sy));
			}
		}
	}
};


Model* modelDuster = createModel<Duster, DusterWidget>("Duster");
