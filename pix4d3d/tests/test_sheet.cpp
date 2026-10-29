#include "catch_amalgamated.hpp"
#include "asec/sheet.hpp"

using namespace asec;
using Catch::Approx;

TEST_CASE("sheet: paper sizes and layout", "[sheet]") {
    double w, h;
    paperSizeMm(Paper::A4, true, w, h); CHECK(w == 297); CHECK(h == 210);
    paperSizeMm(Paper::A3, false, w, h); CHECK(w == 297); CHECK(h == 420);
    SheetSpec s; s.denom = 40;
    SheetLayout L = layoutSheet(s, 5.0, 2.0);   // 5 m × 2 m at 1:40 = 125 × 50 mm
    CHECK(L.contentW == Approx(125)); CHECK(L.contentH == Approx(50));
    CHECK(L.fits);
    CHECK(L.plotX + L.plotW <= L.frameX + L.frameW + 1e-9);
    CHECK(L.plotY + L.plotH <= L.titleY + 1e-9);
    CHECK(L.fitLenM == Approx(L.plotW * 40 / 1000));
}

TEST_CASE("sheet: overflow advice (real Jeju size 38 m)", "[sheet]") {
    SheetSpec s; s.denom = 40;
    SheetAdvice a = adviseSheet(s, 38.0, 5.0);
    REQUIRE(a.overflow);
    CHECK(a.fitLenM < 38.0);
    CHECK(a.smallerDenom == 200);          // 1:100 = 380 mm > A4 plot, 1:200 = 190 mm fits
    CHECK_FALSE(a.a3Fits);                 // 1:40 → 950 mm
    CHECK(a.splitCols == 4);               // 950 / ~233 mm
    CHECK(a.splitRows == 1);
    CHECK(a.splitSheets == 4);
    SheetSpec s3 = s; s3.paper = Paper::A3;
    SheetAdvice b = adviseSheet(s3, 38.0, 5.0);
    CHECK(b.splitSheets == 3);
    CHECK(b.smallerDenom == 200);       // A3 그림 칸 ~368 mm < 1:100 의 380 mm
    // 들어가면 추천 없음
    SheetSpec t = s; t.denom = 200;
    CHECK_FALSE(adviseSheet(t, 38.0, 6.0).overflow);
}

TEST_CASE("sheet: fit denominator is the tightest integer scale", "[sheet]") {
    SheetSpec s;
    double d = fitDenominator(s, 38.0, 6.0);
    SheetSpec t = s; t.denom = d;
    CHECK(layoutSheet(t, 38.0, 6.0).fits);
    t.denom = d - 1;
    CHECK_FALSE(layoutSheet(t, 38.0, 6.0).fits);
}

TEST_CASE("sheet: azimuth text, facing, names, recent list", "[sheet]") {
    CHECK(formatAzimuth(90.4) == "N 90.4\xC2\xB0 E");
    CHECK(formatAzimuth(270) == "N 90.0\xC2\xB0 W");
    CHECK(formatAzimuth(-10) == "N 10.0\xC2\xB0 W");
    CHECK(facingKo(90) == "북쪽을 봄");      // 서→동 선은 북쪽(왼쪽 법선)을 봄
    CHECK(facingKo(270) == "남쪽을 봄");
    CHECK(facingKo(0) == "서쪽을 봄");
    CHECK(facingKo(135) == "북동쪽을 봄");
    CHECK(sectionLetterName(0) == "A\xE2\x80\x93" "A\xE2\x80\xB2");
    CHECK(sectionLetterName(1) == "B\xE2\x80\x93" "B\xE2\x80\xB2");
    CHECK(sectionLetterName(26) == "A1\xE2\x80\x93" "A1\xE2\x80\xB2");
    auto r = pushRecent({"C:/a.3mx", "D:/b.3mx"}, "c:\\A.3MX", 8);
    REQUIRE(r.size() == 2);
    CHECK(r[0] == "c:\\A.3MX"); CHECK(r[1] == "D:/b.3mx");
    std::vector<std::string> many;
    for (int i = 0; i < 12; ++i) many.push_back("f" + std::to_string(i));
    CHECK(pushRecent(many, "new", 8).size() == 8);
    CHECK(pushRecent(many, "new", 8)[0] == "new");
}

TEST_CASE("sheet: section list json round trip", "[sheet]") {
    std::vector<SavedSection> v(2);
    v[0].name = sectionLetterName(0); v[0].ax = 148076; v[0].ay = 98129.5; v[0].bx = 148114; v[0].by = 98129.5; v[0].back = 5; v[0].note = "1호 수혈 장축";
    v[1].name = sectionLetterName(1); v[1].ax = 148090; v[1].ay = 98120; v[1].bx = 148090; v[1].by = 98140; v[1].front = 0.2; v[1].back = 0.5;
    std::string t = sectionsToJson(v, "Production_2.3mx", "EPSG:5186", 1);
    std::vector<SavedSection> w; int cur = -1; std::string err;
    REQUIRE(sectionsFromJson(t, w, &cur, &err));
    REQUIRE(w.size() == 2);
    CHECK(cur == 1);
    CHECK(w[0].name == v[0].name); CHECK(w[0].note == v[0].note);
    CHECK(w[0].ax == 148076); CHECK(w[0].back == 5);
    CHECK(w[1].by == 98140); CHECK(w[1].front == Approx(0.2));
    CHECK_FALSE(sectionsFromJson("{\"x\":1}", w, &cur, &err));
    CHECK_FALSE(sectionsFromJson("not json", w, &cur, &err));
}
