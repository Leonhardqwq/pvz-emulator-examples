#pragma once
#include "object/scene.h"
#include "object/zombie.h"
#include "system/rng.h"
#include "system/reanim.h"
#include "system/zombie/zombie.h"

namespace pvz_emulator::system {

class zombie_factory {
private:
    object::scene& scene;
    object::scene::spawn_data& data;
    system::reanim reanim;
    system::rng rng;

    zombie_subsystems subsystems;

    bool can_spawn_at_row(object::zombie_type type, unsigned int row);

    void create_pool_or_night_lurking(object::zombie_type type, unsigned int row, unsigned int col);
    void create_roof_lurking(object::zombie_type type, unsigned int row, unsigned int col);

public:
    unsigned int get_spawn_row(object::zombie_type type);
    zombie_factory(object::scene& s) :
        scene(s),
        data(s.spawn),
        reanim(s),
        rng(s),
        subsystems(s) {}

    object::zombie& create(object::zombie_type type, int specified_row = -1);
    // impIndex 测试入口：普通出怪仍使用 create()，这里只控制对象池编号相对位置。
    object::zombie& create_before(object::zombie_type type, int ref_index, int specified_row = -1);
    object::zombie& create_after(object::zombie_type type, int ref_index, int specified_row = -1);
    void create_lurking(object::zombie_type type, unsigned int row, unsigned int col);

    void destroy(object::zombie& z);
};

}
