#include "plugin.hpp"

#include <cmath>
#include <string>


struct Duster : Module {
	Duster() {
		config(0, 0, 0, 0);
	}
};


struct DusterWidget : ModuleWidget {
	static constexpr int kNumFrames = 5;
	static constexpr int kCenterFrame = 2;

	// Bristles flop opposite to handle motion; gain scales pixel velocity → mm displacement target.
	static constexpr float kTargetGain = 1.5f;
	static constexpr float kStiffness = 0.18f;
	static constexpr float kDamping = 0.20f;
	static constexpr float kFrameStepMm = 1.2f;

	app::SvgPanel* panel = nullptr;
	std::shared_ptr<window::Svg> frames[kNumFrames];

	math::Vec lastPos;
	float bristleX = 0.f;
	float bristleV = 0.f;
	int currentFrame = kCenterFrame;

	DusterWidget(Duster* module) {
		setModule(module);

		for (int i = 0; i < kNumFrames; ++i) {
			std::string path = asset::plugin(
				pluginInstance, "res/Duster_frame_" + std::to_string(i) + ".svg");
			frames[i] = window::Svg::load(path);
		}

		panel = createPanel<app::SvgPanel>(asset::plugin(
			pluginInstance,
			"res/Duster_frame_" + std::to_string(kCenterFrame) + ".svg"));
		setPanel(panel);

		lastPos = box.pos;
	}

	// Free movement: skip ModuleWidget's rack-aware drag handlers so the
	// Duster doesn't snap to the rail grid and doesn't shove other modules.
	// Right/middle buttons still fall through for context menu, etc.

	void onDragStart(const DragStartEvent& e) override {
		if (e.button != GLFW_MOUSE_BUTTON_LEFT) {
			ModuleWidget::onDragStart(e);
		}
	}

	void onDragEnd(const DragEndEvent& e) override {
		if (e.button != GLFW_MOUSE_BUTTON_LEFT) {
			ModuleWidget::onDragEnd(e);
		}
	}

	void onDragMove(const DragMoveEvent& e) override {
		if (e.button != GLFW_MOUSE_BUTTON_LEFT) {
			ModuleWidget::onDragMove(e);
			return;
		}
		float zoom = getAbsoluteZoom();
		if (zoom > 0.f) {
			box.pos = box.pos.plus(e.mouseDelta.div(zoom));
		}
	}

	void step() override {
		math::Vec delta = box.pos.minus(lastPos);
		lastPos = box.pos;

		float target = -delta.x * kTargetGain;
		float force = (target - bristleX) * kStiffness - bristleV * kDamping;
		bristleV += force;
		bristleX += bristleV;

		int idx = (int) std::round(bristleX / kFrameStepMm) + kCenterFrame;
		if (idx < 0) idx = 0;
		if (idx >= kNumFrames) idx = kNumFrames - 1;

		if (idx != currentFrame) {
			panel->setBackground(frames[idx]);
			currentFrame = idx;
		}

		ModuleWidget::step();
	}
};


Model* modelDuster = createModel<Duster, DusterWidget>("Duster");
