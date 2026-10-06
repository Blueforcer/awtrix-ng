#pragma once

typedef struct bvm bvm;
namespace awtrix { class Canvas; struct RenderCtx; }

namespace awtrix::script {
int b_layout_prepare(bvm* vm);
int b_layout_update(bvm* vm);
int b_layout_draw(bvm* vm);
int b_layout_release(bvm* vm);
}
