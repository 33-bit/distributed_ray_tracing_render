#pragma once
// BVH acceleration structure (optional, off by default — see Scene::use_bvh).
//
// Built once per Scene from every Hittable that reports a finite bounding_box()
// (spheres/triangles/boxes); unbounded primitives (infinite planes) stay out of
// the tree and are tested linearly by the caller (see Scene::hit()).
//
// Construction: recursively split the object list on its bounding box's
// longest axis, sorting by centroid and dividing in half — a simple median
// split, not SAH, but enough to turn O(n) into ~O(log n) for typical scenes.
// Leaves hold up to 4 objects and fall back to a linear scan among them.
#include <algorithm>
#include <memory>
#include <vector>
#include "scene/object.hpp"
#include "scene/aabb.hpp"

struct BVHNode : Hittable {
    std::unique_ptr<BVHNode> left, right;
    std::vector<Hittable*> leaf_objects;   // non-empty only on leaves
    AABB box;

    bool hit(const Ray& r, double tmin, double tmax, HitRecord& rec) const override {
        if (!box.hit(r, tmin, tmax)) return false;

        if (!leaf_objects.empty()) {
            bool found = false;
            double closest = tmax;
            HitRecord tmp;
            for (Hittable* o : leaf_objects)
                if (o->hit(r, tmin, closest, tmp)) { found = true; closest = tmp.t; rec = tmp; }
            return found;
        }

        HitRecord tmp;
        bool hit_left = left  && left->hit(r, tmin, tmax, tmp);
        if (hit_left) { rec = tmp; tmax = tmp.t; }
        bool hit_right = right && right->hit(r, tmin, tmax, tmp);
        if (hit_right) rec = tmp;
        return hit_left || hit_right;
    }

    bool bounding_box(AABB& out) const override { out = box; return true; }

    static std::unique_ptr<BVHNode> build(std::vector<Hittable*>& objs, size_t lo, size_t hi) {
        auto node = std::make_unique<BVHNode>();

        AABB bb;
        for (size_t i = lo; i < hi; ++i) {
            AABB b;
            objs[i]->bounding_box(b);
            bb = (i == lo) ? b : AABB::surrounding(bb, b);
        }
        node->box = bb;

        const size_t count = hi - lo;
        const size_t LEAF_SIZE = 4;
        if (count <= LEAF_SIZE) {
            node->leaf_objects.assign(objs.begin() + lo, objs.begin() + hi);
            return node;
        }

        int axis = bb.longest_axis();
        std::sort(objs.begin() + lo, objs.begin() + hi, [axis](Hittable* a, Hittable* b) {
            AABB ba, bb2;
            a->bounding_box(ba);
            b->bounding_box(bb2);
            double ca = (axis == 0) ? ba.centroid().x : (axis == 1) ? ba.centroid().y : ba.centroid().z;
            double cb = (axis == 0) ? bb2.centroid().x : (axis == 1) ? bb2.centroid().y : bb2.centroid().z;
            return ca < cb;
        });

        size_t mid = lo + count / 2;
        node->left  = build(objs, lo, mid);
        node->right = build(objs, mid, hi);
        return node;
    }
};
