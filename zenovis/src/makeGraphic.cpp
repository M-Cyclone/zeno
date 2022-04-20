#include <zenovis/Scene.h>
#include <zeno/core/IObject.h>
#include <zeno/utils/cppdemangle.h>
#include <zeno/utils/log.h>
#include <zenovis/IGraphic.h>
#include <zenovis/makeGraphic.h>

namespace zenovis {

std::unique_ptr<IGraphic> makeGraphic(Scene *scene, std::shared_ptr<zeno::IObject> obj) {
    if (auto ig = makeGraphicPrimitive(scene, obj)) {
        zeno::log_trace("load_object: primitive");
        return ig;
    }

    if (auto ig = makeGraphicLight(scene, obj)) {
        zeno::log_trace("load_object: light");
        return ig;
    }

    if (auto ig = makeGraphicCamera(scene, obj)) {
        zeno::log_trace("load_object: camera");
        return ig;
    }

    zeno::log_debug("load_object: unexpected view object {}",
                    zeno::cppdemangle(typeid(*obj)));

    //printf("%s\n", ext.c_str());
    //assert(0 && "bad file extension name");
    return nullptr;
}

} // namespace zenovis