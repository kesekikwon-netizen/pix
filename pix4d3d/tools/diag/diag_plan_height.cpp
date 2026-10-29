// diag: plan ortho + max-Z height map of an area at leaf LOD
#include <cstdio>
#include <cmath>
#include <algorithm>
#include "asec/engine.hpp"
#include "stb_image.h"
#include "stb_image_write.h"
using namespace asec;
static void decodeTex(Texture& t){int w,h,c;unsigned char*p=stbi_load_from_memory(t.encoded.data(),int(t.encoded.size()),&w,&h,&c,4);if(!p)return;t.rgba.w=w;t.rgba.h=h;t.rgba.px.assign(p,p+size_t(w)*h*4);stbi_image_free(p);}
int main(int argc,char**argv){
  if(argc<7){fprintf(stderr,"plan scene cx cy half res out\n");return 2;}
  double cx=atof(argv[2]),cy=atof(argv[3]),hs=atof(argv[4]),res=atof(argv[5]);std::string out=argv[6];
  TmxSource s; s.cache->textureDecoder=decodeTex; std::string err;
  if(!s.open(fs::u8path(argv[1]),&err)){fprintf(stderr,"%s\n",err.c_str());return 1;}
  Vec3 o=s.srs.origin; printf("origin %.3f %.3f %.3f\n",o.x,o.y,o.z);
  double lx=cx-o.x, ly=cy-o.y;
  Box3 b; b.add(Vec3(lx-hs,ly-hs,-1e9)); b.add(Vec3(lx+hs,ly+hs,1e9));
  std::vector<MeshPtr> ms; LeafStats st;
  double lres=argc>7?atof(argv[7]):res; if(!s.areaMeshes(b,lres,ms,&st,&err)){fprintf(stderr,"%s\n",err.c_str());return 1;}
  printf("meshes=%zu tris=%zu leaf=%zu depth=%d fallback=%zu\n",ms.size(),st.triangles,st.leafNodes,st.maxDepth,st.fallbackNodes);
  int W=int(2*hs/res),H=W; RgbaImage img;
  renderPlan(ms,lx-hs,ly+hs,res,W,H,img);
  stbi_write_png((out+"_ortho.png").c_str(),img.w,img.h,4,img.px.data(),img.w*4);
  // height raster (max z)
  std::vector<float> zb(size_t(W)*H,-1e30f);
  for(auto&m:ms){auto&P=m->pos;for(size_t t=0;t+2<m->idx.size();t+=3){
    const float*a=&P[3*m->idx[t]],*bq=&P[3*m->idx[t+1]],*c=&P[3*m->idx[t+2]];
    auto px=[&](const float*v,double&x,double&y){x=(v[0]-(lx-hs))/res;y=((ly+hs)-v[1])/res;};
    double x0,y0,x1,y1,x2,y2;px(a,x0,y0);px(bq,x1,y1);px(c,x2,y2);
    int mnx=std::max(0,(int)floor(std::min({x0,x1,x2}))),mxx=std::min(W-1,(int)ceil(std::max({x0,x1,x2})));
    int mny=std::max(0,(int)floor(std::min({y0,y1,y2}))),mxy=std::min(H-1,(int)ceil(std::max({y0,y1,y2})));
    double den=(y1-y2)*(x0-x2)+(x2-x1)*(y0-y2); if(fabs(den)<1e-12)continue;
    for(int y=mny;y<=mxy;++y)for(int x=mnx;x<=mxx;++x){double X=x+.5,Y=y+.5;
      double w0=((y1-y2)*(X-x2)+(x2-x1)*(Y-y2))/den,w1=((y2-y0)*(X-x2)+(x0-x2)*(Y-y2))/den,w2=1-w0-w1;
      if(w0<-1e-6||w1<-1e-6||w2<-1e-6)continue;float z=float(w0*a[2]+w1*bq[2]+w2*c[2]);float&r=zb[size_t(y)*W+x];if(z>r)r=z;}}}
  std::vector<float> v;for(float z:zb)if(z>-1e29f)v.push_back(z);std::sort(v.begin(),v.end());
  double lo=v[v.size()*2/100],hi=v[v.size()*98/100];printf("z p2=%.3f p50=%.3f p98=%.3f min=%.3f max=%.3f\n",lo+o.z,v[v.size()/2]+o.z,hi+o.z,v.front()+o.z,v.back()+o.z);
  std::vector<unsigned char> g(size_t(W)*H*3);
  for(size_t i=0;i<zb.size();++i){double t=zb[i]>-1e29f?std::clamp((zb[i]-lo)/(hi-lo),0.0,1.0):0;
    // turbo-ish ramp
    double r=std::clamp(1.5-fabs(4*t-3),0.0,1.0),gg=std::clamp(1.5-fabs(4*t-2),0.0,1.0),bb=std::clamp(1.5-fabs(4*t-1),0.0,1.0);
    g[3*i]=r*255;g[3*i+1]=gg*255;g[3*i+2]=bb*255;}
  stbi_write_png((out+"_height.png").c_str(),W,H,3,g.data(),W*3);
  FILE*f=fopen((out+"_height.f32").c_str(),"wb");fwrite(zb.data(),4,zb.size(),f);fclose(f);
  printf("W=%d H=%d\n",W,H);
}
