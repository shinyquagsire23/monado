// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  SDL2 functions to drive the GUI.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup gui
 */


#include "gui_common.h"

#include <vector>


struct gui_scene_manager
{
public:
	std::vector<gui_scene *> scenes = {};
	std::vector<gui_scene *> del = {};
};

extern "C" void
gui_scene_push_front(struct gui_program *p, struct gui_scene *me)
{
	auto &gsm = *p->gsm;

	// Need to remove the scene if it is already on the list.
	auto index = gsm.scenes.begin();
	for (auto scene : gsm.scenes) {
		if (scene != me) {
			index++;
			continue;
		}

		gsm.scenes.erase(index);
		break;
	}

	// Now push it to the front.
	gsm.scenes.push_back(me);
}

extern "C" void
gui_scene_delete_me(struct gui_program *p, struct gui_scene *me)
{
	auto &gsm = *p->gsm;

	auto index = gsm.scenes.begin();
	for (auto scene : gsm.scenes) {
		if (scene != me) {
			index++;
			continue;
		}

		gsm.scenes.erase(index);
		break;
	}

	gsm.del.push_back(me);
}

extern "C" void
gui_scene_manager_render(struct gui_program *p)
{
	auto &gsm = *p->gsm;
	auto copy = gsm.scenes;

	for (auto scene : copy) {
		scene->render(scene, p);
	}

	copy = gsm.del;
	gsm.del.clear();
	for (auto scene : copy) {
		scene->destroy(scene, p);
	}

	// If there are no scenes left stop the program.
	if (gsm.scenes.empty()) {
		p->stopped = true;
	}
}

extern "C" void
gui_scene_manager_init(struct gui_program *p)
{
	p->gsm = new gui_scene_manager;
}

extern "C" void
gui_scene_manager_destroy(struct gui_program *p)
{
	for (auto scene : p->gsm->scenes) {
		scene->destroy(scene, p);
	}

	delete p->gsm;
	p->gsm = NULL;
}
