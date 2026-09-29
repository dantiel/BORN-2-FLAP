using System;
using System.IO;
using System.Drawing;
using System.Drawing.Imaging;
using System.Drawing.Drawing2D;
using System.Runtime.InteropServices;
using System.Globalization;
public static class ShiomoriTextureBuilder {
 const int N=2048;
 static byte Byte(double a){return (byte)Math.Max(0,Math.Min(255,Math.Round(a)));}
 static double Smooth(double t){return t*t*(3-2*t);}
 static double[] Noise(int grid,int seed){
  Random r=new Random(seed);double[] lattice=new double[grid*grid];for(int i=0;i<lattice.Length;i++)lattice[i]=r.NextDouble()*2-1;
  double[] result=new double[N*N];
  for(int y=0;y<N;y++){double gy=(double)y*grid/N;int iy=(int)gy;double fy=Smooth(gy-iy);
   for(int x=0;x<N;x++){double gx=(double)x*grid/N;int ix=(int)gx;double fx=Smooth(gx-ix);
    double a=lattice[iy*grid+ix]*(1-fx)+lattice[iy*grid+(ix+1)%grid]*fx;
    double b=lattice[((iy+1)%grid)*grid+ix]*(1-fx)+lattice[((iy+1)%grid)*grid+(ix+1)%grid]*fx;
    result[y*N+x]=a*(1-fy)+b*fy;
   }
  } return result;
 }
 static Bitmap BitmapFrom(byte[] rgb){
  Bitmap b=new Bitmap(N,N,PixelFormat.Format24bppRgb);
  BitmapData d=b.LockBits(new Rectangle(0,0,N,N),ImageLockMode.WriteOnly,PixelFormat.Format24bppRgb);
  byte[] buffer=new byte[d.Stride*N];
  for(int y=0;y<N;y++)for(int x=0;x<N;x++){int a=(y*N+x)*3,z=y*d.Stride+x*3;buffer[z]=rgb[a+2];buffer[z+1]=rgb[a+1];buffer[z+2]=rgb[a];}
  Marshal.Copy(buffer,0,d.Scan0,buffer.Length);b.UnlockBits(d);return b;
 }
 static void Save(Bitmap b,string path,bool master){
  if(File.Exists(path))throw new IOException("Refusing to overwrite existing texture: "+path);
  Directory.CreateDirectory(Path.GetDirectoryName(path));
  ImageCodecInfo jpg=null;foreach(var c in ImageCodecInfo.GetImageEncoders())if(c.MimeType=="image/jpeg")jpg=c;
  using(var p=new EncoderParameters(1)){p.Param[0]=new EncoderParameter(System.Drawing.Imaging.Encoder.Quality,100L);b.Save(path,jpg,p);}
  if(master)b.Save(Path.ChangeExtension(path,".png"),ImageFormat.Png);
 }
 public static void Colour(string source,string destination){
  using(Bitmap src=new Bitmap(source))using(Bitmap b=new Bitmap(N,N,PixelFormat.Format24bppRgb)){
   using(Graphics g=Graphics.FromImage(b))using(ImageAttributes attrs=new ImageAttributes()){
    g.InterpolationMode=InterpolationMode.HighQualityBicubic;g.PixelOffsetMode=PixelOffsetMode.HighQuality;attrs.SetWrapMode(WrapMode.Tile);
    g.DrawImage(src,new Rectangle(0,0,N,N),0,0,src.Width,src.Height,GraphicsUnit.Pixel,attrs);
   } Save(b,destination,false);
  }
 }
 static byte[] Normal(double[] h,double strength){
  byte[] outp=new byte[N*N*3];
  for(int y=0;y<N;y++)for(int x=0;x<N;x++){
   double dx=(h[y*N+(x+1)%N]-h[y*N+(x+N-1)%N])*.5*strength;
   double dy=(h[((y+1)%N)*N+x]-h[((y+N-1)%N)*N+x])*.5*strength;
   // Image coordinates: x right, y down. DirectX = (-dH/dx,-dH/dy,+1).
   // OpenGL image convention would have the opposite encoded green sign.
   double inv=1/Math.Sqrt(1+dx*dx+dy*dy);int k=(y*N+x)*3;
   outp[k]=Byte(127.5*(1-dx*inv));outp[k+1]=Byte(127.5*(1-dy*inv));outp[k+2]=Byte(127.5*(1+inv));
  }return outp;
 }
 public static void Technical(string root){
  double[] waves=new double[N*N];
  for(int y=0;y<N;y++)for(int x=0;x<N;x++){
   double u=(double)x/N,v=(double)y/N;
   double phase=2*Math.PI*(4*v+.015*Math.Sin(2*Math.PI*u)+.004*Math.Sin(4*Math.PI*u));
   waves[y*N+x]=Math.Sin(phase)+.20*Math.Sin(2*phase+.45)+.045*Math.Sin(3*phase+1.1);
  }
  using(var b=BitmapFrom(Normal(waves,45)))Save(b,Path.Combine(root,"ocean_waves/nor.jpg"),true);
  foreach(string name in new[]{"sand_beach","white_panel"}){
   bool sand=name=="sand_beach";
   double[] h=Noise(sand?512:640,sand?7351:9182), detail=Noise(1024,sand?2483:6301), broad=Noise(64,sand?1112:8513);
   byte[] rough=new byte[N*N*3];
   for(int i=0;i<h.Length;i++){
    h[i]=.68*h[i]+.27*detail[i]+.05*broad[i];
    byte val=Byte(255*((sand?.88:.69)+(sand?.055:.035)*h[i]));rough[3*i]=rough[3*i+1]=rough[3*i+2]=val;
   }
   using(var b=BitmapFrom(Normal(h,sand?.85:.24)))Save(b,Path.Combine(root,name+"/nor.jpg"),true);
   using(var b=BitmapFrom(rough))Save(b,Path.Combine(root,name+"/Rough.jpg"),true);
  }
 }
 static byte[] Read(string path){
  using(Bitmap src=new Bitmap(path))using(Bitmap b=new Bitmap(N,N,PixelFormat.Format24bppRgb)){
   if(src.Width!=N||src.Height!=N)throw new Exception("Wrong dimensions: "+path);
   using(Graphics g=Graphics.FromImage(b))g.DrawImageUnscaled(src,0,0);
   BitmapData d=b.LockBits(new Rectangle(0,0,N,N),ImageLockMode.ReadOnly,PixelFormat.Format24bppRgb);byte[] raw=new byte[d.Stride*N],rgb=new byte[N*N*3];Marshal.Copy(d.Scan0,raw,0,raw.Length);b.UnlockBits(d);
   for(int y=0;y<N;y++)for(int x=0;x<N;x++){int k=(y*N+x)*3,z=y*d.Stride+x*3;rgb[k]=raw[z+2];rgb[k+1]=raw[z+1];rgb[k+2]=raw[z];}return rgb;
  }
 }
 public static string Validate(string root){
  string report="Texture validation (decoded delivery JPGs, no ICC/gamma transform applied):\n";
  foreach(string path in Directory.GetFiles(root,"*.jpg",SearchOption.AllDirectories)){
   string rel=path.Substring(root.Length+1).Replace('\\','/');if(!(rel.StartsWith("sand_beach/")||rel.StartsWith("white_panel/")||rel.StartsWith("ocean_waves/")))continue;
   byte[] p=Read(path);double ex=0,ey=0,ix=0,iy=0,meanR=0,meanG=0,meanB=0,lenerr=0,rx=0,gy=0;int n=0;
   for(int y=0;y<N;y++)for(int x=0;x<N;x++){
    int k=(y*N+x)*3;meanR+=p[k];meanG+=p[k+1];meanB+=p[k+2];
    for(int c=0;c<3;c++){
     if(x==0)ex+=Math.Abs(p[k+c]-p[(y*N+N-1)*3+c]);else {ix+=Math.Abs(p[k+c]-p[k-3+c]);}
     if(y==0)ey+=Math.Abs(p[k+c]-p[((N-1)*N+x)*3+c]);else {iy+=Math.Abs(p[k+c]-p[k-N*3+c]);}
    }
    if(Path.GetFileName(path)=="nor.jpg"){
     double a=p[k]/127.5-1,b=p[k+1]/127.5-1,c=p[k+2]/127.5-1;lenerr+=Math.Abs(Math.Sqrt(a*a+b*b+c*c)-1);rx+=a*a;gy+=b*b;
     if(c<=0)throw new Exception("Nonpositive normal Z "+rel);
    } n++;
   }
   ex/=N*3;ey/=N*3;ix/=(N-1)*N*3;iy/=(N-1)*N*3;
   report+=String.Format(CultureInfo.InvariantCulture,"{0}: 2048x2048 RGB, mean=({1:F2},{2:F2},{3:F2}), seam X/Y={4:F3}/{5:F3}, interior X/Y={6:F3}/{7:F3}, mean normal length error={8:F5}\n",rel,meanR/n,meanG/n,meanB/n,ex,ey,ix,iy,lenerr/n);
   if(Path.GetFileName(path)=="nor.jpg" && lenerr/n>.015)throw new Exception("Excessive normal quantization error "+rel);
   if(rel.StartsWith("ocean_waves/")){
    double ratio=Math.Sqrt(gy/Math.Max(rx,1e-12));report+="Wave Y/X slope RMS ratio="+ratio.ToString("F2",CultureInfo.InvariantCulture)+" (horizontal crests)\n";
    if(ratio<15)throw new Exception("Wave direction validation failed");
   }
  }return report;
 }
}
