#include "render/MusketHudLayout.h"

#include <doctest/doctest.h>

#include <array>
#include <cmath>
#include <limits>

using namespace valcraft;

TEST_CASE("le reticule du fusil reste centre sur tous les ratios demandes") {
    struct Viewport {
        int width = 0;
        int height = 0;
    };
    constexpr std::array viewports {
        Viewport {1'024, 768},
        Viewport {1'920, 1'080},
        Viewport {3'440, 1'440},
        Viewport {2'560, 1'440},
    };

    for (const auto viewport : viewports) {
        const auto layout =
            resolve_musket_hud_layout(
                viewport.width,
                viewport.height,
                0.0F);
        REQUIRE(layout.valid);
        CHECK(layout.center_x ==
              doctest::Approx(
                  static_cast<float>(
                      viewport.width) *
                  0.5F));
        CHECK(layout.center_y ==
              doctest::Approx(
                  static_cast<float>(
                      viewport.height) *
                  0.5F));
        REQUIRE(layout.branches.size() == 4U);

        const auto& left = layout.branches[0];
        const auto& right = layout.branches[1];
        const auto& top = layout.branches[2];
        const auto& bottom = layout.branches[3];
        CHECK(left.x + left.width <
              layout.center_x);
        CHECK(right.x >
              layout.center_x);
        CHECK(top.y + top.height <
              layout.center_y);
        CHECK(bottom.y >
              layout.center_y);
        CHECK(
            layout.center_x -
                (left.x + left.width) ==
            doctest::Approx(
                right.x -
                layout.center_x));
        CHECK(
            layout.center_y -
                (top.y + top.height) ==
            doctest::Approx(
                bottom.y -
                layout.center_y));
    }
}

TEST_CASE("l ADS compacte les quatre branches sans deplacer le point rouge") {
    const auto hip =
        resolve_musket_hud_layout(
            1'920,
            1'080,
            0.0F);
    const auto ads =
        resolve_musket_hud_layout(
            1'920,
            1'080,
            1.0F);

    REQUIRE(hip.valid);
    REQUIRE(ads.valid);
    CHECK(ads.center_x == hip.center_x);
    CHECK(ads.center_y == hip.center_y);
    CHECK(ads.dot_size == hip.dot_size);
    CHECK(ads.branches[1].x <
          hip.branches[1].x);
    CHECK(ads.branches[3].y <
          hip.branches[3].y);
}

TEST_CASE("la geometrie HiDPI suit les pixels et assainit ses entrees") {
    const auto normal =
        resolve_musket_hud_layout(
            1'280,
            720,
            0.5F);
    const auto hidpi =
        resolve_musket_hud_layout(
            2'560,
            1'440,
            0.5F);
    REQUIRE(normal.valid);
    REQUIRE(hidpi.valid);
    CHECK(hidpi.scale ==
          doctest::Approx(normal.scale * 2.0F));
    CHECK(hidpi.dot_size ==
          doctest::Approx(normal.dot_size * 2.0F));

    const auto nan =
        std::numeric_limits<float>::quiet_NaN();
    const auto invalid_aim =
        resolve_musket_hud_layout(
            1'920,
            1'080,
            nan);
    const auto hip =
        resolve_musket_hud_layout(
            1'920,
            1'080,
            0.0F);
    CHECK(invalid_aim.branches ==
          hip.branches);
    CHECK_FALSE(
        resolve_musket_hud_layout(
            0,
            1'080,
            0.0F)
            .valid);
}
