// Wavefront OBJ(+MTL map_Kd) 읽기, iTwin/ContextCapture metadata.xml(SRS, SRSOrigin) 찾기.
#pragma once
#include "asec/tmx.hpp"

namespace asec {
/// 재질(텍스처)별 메시로 나눠 읽는다. 텍스처는 encoded 바이트만 채움(앱이 디코드).
bool loadObj(const fs::path& objPath, std::vector<MeshPtr>& out, std::string* err);
/// metadata.xml 파싱: <SRS>EPSG:5186</SRS>, <SRSOrigin>x,y,z</SRSOrigin>
bool parseMetadataXml(const std::string& xml, SrsInfo& out);
/// OBJ 폴더부터 위로 3단계까지 metadata.xml 탐색
bool findMetadataXml(const fs::path& objPath, SrsInfo& out, fs::path* found = nullptr);
}  // namespace asec
