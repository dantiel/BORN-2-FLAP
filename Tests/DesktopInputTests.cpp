#include "born2flap_desktop_input.h"
#include <iostream>
#include <stdexcept>

void require(bool value, const char* message)
{
    if (!value) throw std::runtime_error(message);
}
bool near(double a, double b) { return std::abs(a-b) < 1e-6; }

int main()
{
    using born2flap::DesktopInput;
    require(near(DesktopInput::KeyboardThrottle(true,true,false),.32), "Ctrl+W low throttle");
    require(near(DesktopInput::KeyboardThrottle(true,false,false),.72), "W normal throttle");
    require(near(DesktopInput::KeyboardThrottle(true,false,true),1), "Shift+W full throttle");
    require(near(DesktopInput::KeyboardThrottle(true,true,true),.32), "Ctrl takes priority over Shift");
    for(int fps : {30,60,144})
    {
        DesktopInput d;
        const double dt=1.0/fps;
        auto step=[&](double throttle=0, bool w=false, double r=0, double p=0, double y=0,
                      double x=0, double my=0, double wheel=0, bool muteY=false, bool muteR=false)
        {d.Step(dt,throttle,w,r,p,y,x,my,wheel,muteY,muteR);};
        step(0,false,0,0,0,0,0,25);
        for(int i=0;i<fps;++i)step();
        require(near(d.throttle,.5)&&d.wheelOwnsThrottle,"wheel throttle must latch");
        for(int i=0;i<fps;++i)step(.72,true);
        require(near(d.throttle,.72)&&near(d.wheelThrottle,.5)&&!d.wheelOwnsThrottle,"W must override without changing wheel memory");
        for(int i=0;i<fps;++i)step();
        require(near(d.throttle,0)&&!d.wheelOwnsThrottle,"W release must glide, not restore the wheel");
        step(0,false,0,0,0,0,0,1);
        for(int i=0;i<fps;++i)step();
        require(near(d.throttle,.52),"wheel resumes from remembered 50%, not keyboard throttle");
        step(.32,true,0,0,0,0,0,1);
        for(int i=0;i<fps;++i)step(.32,true);
        require(near(d.throttle,.32)&&near(d.wheelThrottle,.54),"held W wins simultaneous wheel input");
        step(0,false,0,0,0,0,0,-.5);
        for(int i=0;i<fps;++i)step();
        require(near(d.throttle,.53),"high-resolution fractional wheel events");
        step(0,false,0,0,0,0,0,1000);
        require(near(d.wheelThrottle,1),"wheel upper clamp");
        step(0,false,0,0,0,0,0,-1000);
        require(near(d.wheelThrottle,0),"wheel lower clamp");
        for(int i=0;i<fps;++i)step(0,false,0,0,0,DesktopInput::MouseTravel*dt,DesktopInput::MouseTravel*.5*dt);
        require(d.roll>.999&&d.yaw>.999&&near(d.pitch,born2flap::RcKeyboard::Expo(.5)),"mouse axes and frame-rate-independent sensitivity");
        const double heldPitch = d.pitch;
        for(int i=0;i<3*fps;++i)step();
        require(near(d.roll,1)&&near(d.yaw,1)&&near(d.pitch,heldPitch),"all mouse channels must hold without motion");
        step(0,false,0,0,0,DesktopInput::MouseTravel*dt,DesktopInput::MouseTravel*.5*dt,0,true,false);
        require(d.roll==0&&d.pitch==0&&d.yaw==0,"left click resets all mouse axes, even with simultaneous motion");
        for(int i=0;i<fps;++i)step(0,false,0,0,0,DesktopInput::MouseTravel*dt,DesktopInput::MouseTravel*.5*dt,0,true,false);
        require(d.yaw==0&&d.roll>.999&&d.pitch>0,"holding left mouse suppresses only yaw while movement builds other axes");
        step(0,false,0,0,0,DesktopInput::MouseTravel*dt,0,0,false,true);
        require(d.roll==0&&d.pitch==0&&d.yaw==0,"right click resets all mouse axes");
        for(int i=0;i<fps;++i)step(0,false,0,0,0,DesktopInput::MouseTravel*dt,DesktopInput::MouseTravel*.5*dt,0,false,true);
        require(d.roll==0&&d.yaw>.999&&d.pitch>0,"holding right mouse suppresses only roll");
        for(int i=0;i<fps;++i)step(0,false,1,0,1,DesktopInput::MouseTravel*dt,0,0,true,true);
        require(d.roll>.999&&d.yaw>.999,"mouse buttons must not suppress keyboard channels");
        for(int i=0;i<2*fps;++i)step(0,false,-1,0,-1,DesktopInput::MouseTravel*dt);
        require(near(d.roll,0)&&near(d.yaw,0),"opposing mouse and keyboard must cancel");
        for(int i=0;i<2*fps;++i)step(0,false,1,0,1,DesktopInput::MouseTravel*dt);
        require(d.roll==1&&d.yaw==1,"additive steering must clamp at full channel");
        for(int i=0;i<fps;++i)step();
        require(near(d.roll,1)&&near(d.yaw,1),"keyboard release must retain mouse deflection");
        step(0,false,0,0,0,0,0,0,true,false);
        step();
        require(d.roll==0&&d.pitch==0&&d.yaw==0,"releasing a click must not restore old mouse deflections");
        step(0,false,0,0,0,DesktopInput::MouseTravel*.5,-DesktopInput::MouseTravel*.5);
        require(near(d.mouseRoll,.5)&&near(d.mouseYaw,.5)&&near(d.mousePitch,-.5),"displacement sets signed stick positions");
        step(0,false,0,0,0,-DesktopInput::MouseTravel*.25,DesktopInput::MouseTravel*.25);
        require(near(d.mouseRoll,.25)&&near(d.mousePitch,-.25),"reverse motion subtracts from stored position");
        step(0,false,0,0,0,0,0,12);
        d.Step(dt,0,false,0,0,0,0,0,0,false,false,true);
        require(d.roll==0&&d.pitch==0&&d.yaw==0&&near(d.wheelThrottle,.24)&&d.wheelOwnsThrottle,
                "a click completed within one frame resets steering without affecting wheel throttle");
        step(0,false,0,0,0,100,-100);
        require(d.roll>0&&d.roll<.025&&d.pitch<0&&d.pitch>-.025,"small signed mouse movements need fine authority");
        step(0,false,0,0,0,-2*DesktopInput::MouseTravel,-2*DesktopInput::MouseTravel);
        for(int i=0;i<fps;++i)step();
        require(near(d.roll,-1)&&near(d.pitch,-1)&&near(d.yaw,-1),"negative mouse channels must reach and hold full travel");
    }
    std::cout << "Desktop input: throttle ownership/memory, low mode, mouse mixing/mutes and 30/60/144 FPS passed\n";
}
