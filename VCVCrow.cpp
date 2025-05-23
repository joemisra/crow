#include "rack.hpp"

using namespace rack;

// Create the plugin instance to be registered in the Rack plugin manager.
Plugin *pluginInstance;

void init(Plugin *p) {
	pluginInstance = p;

	// Add modules here
	p->addModel(modelCrow);

	// Any other plugin initialization may go here.
	// As an alternative, consider lazy-loading assets and lookup tables when your module is created to reduce startup times of Rack.
}

// Forward declare the model
Model *modelCrow;
