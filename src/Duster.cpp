#include "plugin.hpp"

#include <cmath>
#include <vector>
#include <settings.hpp>

// Temporary visualisation of cable obstacles while we figure out why the
// bristles aren't reacting. Set to 0 to disable.
#define DUSTER_DEBUG_OBSTACLES 1


struct Duster : Module {
	Duster() {
		config(0, 0, 0, 0);
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

	Bristle bristles[kNumBristles];

	// Parent DusterWidget; we read its box.pos as the brush position and skip
	// it when collecting obstacles.
	widget::Widget* brush = nullptr;
	math::Vec lastBrushPos;
	bool firstStep = true;

	std::vector<math::Rect> rectObstacles;
	std::vector<SegmentObstacle> segObstacles;

	void setBrush(widget::Widget* b) {
		brush = b;
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

		for (int i = 0; i < kNumBristles; ++i) {
			const BristleDef& d = kBristles[i];
			Bristle& b = bristles[i];

			// Drag target: tips lag opposite to handle motion.
			float tX = -velMmX * kDragGainX;
			float tY = -velMmY * kDragGainY;

			// Damped spring toward target.
			float fX = (tX - b.dispX) * kStiffness - b.velX * kDamping;
			float fY = (tY - b.dispY) * kStiffness - b.velY * kDamping;
			b.velX += fX;
			b.velY += fY;
			b.dispX += b.velX;
			b.dispY += b.velY;

			// Anchor (fixed) and tip (moving) in rack pixels — same space as
			// the obstacle rects and segments.
			float anchorPxX = brushOrigin.x + d.anchorX * mm;
			float anchorPxY = brushOrigin.y + d.anchorY * mm;
			float tipPxX = brushOrigin.x + (d.restX + b.dispX) * mm;
			float tipPxY = brushOrigin.y + (d.restY + b.dispY) * mm;

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

		for (int i = 0; i < kNumBristles; ++i) {
			const BristleDef& d = kBristles[i];
			const Bristle& b = bristles[i];
			float ax = d.anchorX * mm;
			float ay = d.anchorY * mm;
			float tx = (d.restX + b.dispX) * mm;
			float ty = (d.restY + b.dispY) * mm;
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
	bool didDrag = false;

	DusterWidget(Duster* module) {
		setModule(module);

		// Order matters: bristle layer added first → drawn behind the panel,
		// so the handle's dark base strip covers the bristle anchors.
		bristleLayer = new BristleWidget();
		bristleLayer->box.size = math::Vec(mm2px(40.64f), mm2px(128.5f));
		bristleLayer->setBrush(this);
		addChild(bristleLayer);

		setPanel(createPanel<app::SvgPanel>(
			asset::plugin(pluginInstance, "res/Duster_handle.svg")));
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
