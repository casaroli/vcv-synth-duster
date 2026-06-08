#include "plugin.hpp"


struct Duster : Module {
	Duster() {
		config(0, 0, 0, 0);
	}
};


struct DusterWidget : ModuleWidget {
	DusterWidget(Duster* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/Duster.svg")));
	}
};


Model* modelDuster = createModel<Duster, DusterWidget>("Duster");
