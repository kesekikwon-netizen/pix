#include <cstdio>
#include <cmath>
#include "asec/engine.hpp"
#include "asec/pick.hpp"
#include "stb_image.h"
using namespace asec;
static void decodeTex(Texture& t){int w,h,c;unsigned char*p=stbi_load_from_memory(t.encoded.data(),int(t.encoded.size()),&w,&h,&c,4);if(!p)return;t.rgba.w=w;t.rgba.h=h;t.rgba.px.assign(p,p+size_t(w)*h*4);stbi_image_free(p);}
int main(int argc,char**argv){
  double ax=atof(argv[2]),ay=atof(argv[3]),bx=atof(argv[4]),by=atof(argv[5]),back=atof(argv[6]),mres=argc>7?atof(argv[7]):0;
  TmxSource s; s.cache->textureDecoder=decodeTex; std::string err; s.open(fs::u8path(argv[1]),&err);
  Vec3 o=s.srs.origin;
  SectionRequest rq; rq.line.a=Vec2(ax-o.x,ay-o.y); rq.line.b=Vec2(bx-o.x,by-o.y); rq.line.front=0; rq.line.back=back; rq.meshRes=mres;
  SectionOutput so; if(!computeSection(s,rq,so,&err)){fprintf(stderr,"%s\n",err.c_str());return 1;}
  fprintf(stderr,"leaf=%zu tris=%zu depth=%d preview=%d polylines=%zu z=[%.3f,%.3f] img=%dx%d\n",so.stats.leafNodes,so.stats.triangles,so.stats.maxDepth,(int)so.previewLod,so.result.profile.size(),so.result.zMin,so.result.zMax,so.image.img.w,so.image.img.h);
  FILE*f=fopen("sec.csv","w"); fprintf(f,"k,s,z\n"); int k=0; for(auto&pl:so.result.profile){++k;for(auto&p:pl)fprintf(f,"%d,%.4f,%.4f\n",k,p.x,p.y);} fclose(f);
  f=fopen("pick.csv","w"); fprintf(f,"s,z,ok\n"); double L=std::hypot(bx-ax,by-ay);
  for(double t=0;t<=L+1e-9;t+=0.01){double X=ax+(bx-ax)*t/L,Y=ay+(by-ay)*t/L; PickResult pr; bool ok=pickVerticalWorld(s,X,Y,pr,&err)&&pr.hit; fprintf(f,"%.4f,%.4f,%d\n",t,ok?pr.world.z:NAN,ok);} fclose(f);
}
