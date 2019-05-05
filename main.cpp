/* Game Controller */
#include <mbed.h>
#include <EthernetInterface.h>
#include <rtos.h>
#include <mbed_events.h>

#include <FXOS8700Q.h>
#include <C12832.h>

/* display */
C12832 lcd(D11, D13, D12, D7, D10);

/* event queue and thread support */
Thread dispatch;
EventQueue periodic;

/* Accelerometer */
I2C i2c(PTE25, PTE24);
FXOS8700QAccelerometer acc(i2c, FXOS8700CQ_SLAVE_ADDR1);

/* Speaker */
PwmOut speaker(D6);

/* play a note */
void playNote(float note) {
  speaker.period(1.0/note);
  speaker.write(0.5);
  wait(0.1);
}

/* play crash song */
void playCrash(){
    playNote(1174.66);
    playNote(880.00);
    playNote(659.25);
    playNote(659.25);
    playNote(659.25);
    speaker.write(0);
}

/* play land song */
void playLand(){
    playNote(880.00);
    speaker.write(0);
    wait(0.5);
    playNote(880.00);
    playNote(1174.66);
    playNote(1174.66);
    playNote(1174.66);
    speaker.write(0);
}

/* Input from Potentiometers */
AnalogIn  left(A0);
AnalogIn  right(A1);

/* switches */
InterruptIn sw3(SW3);
InterruptIn sw2(SW2);

/* LEDs */
DigitalOut red(PTB22);
DigitalOut green(LED_GREEN);

/* joystick and buttons */
enum { Btn1, Btn2, sw_up, sw_down, sw_left, sw_right, sw_center};
const char *swname[] = {"SW2","SW3","Up","Down","Left","Right","Center"};
struct pushbutton {
    DigitalIn sw;
    bool invert;
} buttons[] = {
  {DigitalIn(SW2),true},
  {DigitalIn(SW3),true},
  {DigitalIn(A2),false},
  {DigitalIn(A3),false},
  {DigitalIn(A4),false},
  {DigitalIn(A5),false},
  {DigitalIn(D4),false},
};

/* User input states */
int throttle = 0;
float roll = 0;

/* lander status */
int fuel, crashed, landed, landable, flying;
float altitude, vx, vy;


/* Polling sensors */
void user_input(void){
    motion_data_units_t a;
    acc.getAxis(a);


    float magnitude = sqrt( a.x*a.x + a.y*a.y + a.z*a.z );
    a.x = a.x/magnitude;
    a.y = a.y/magnitude;
    a.y = a.y/magnitude;

    float angle = asin(a.x);
    float angleDegrees;
    angleDegrees = (angle*180)/3.14;
    angle = angleDegrees/90;

    roll = -angle;
    if(roll <= 0.1 && roll >= -0.1) {
        roll = 0;
    }

    /*TODO decide on what throttle setting 0..100 to ask for */
    if(sw2.read() == 0) {
        throttle = 100;
    }
    else if(sw3.read() == 0) {
        throttle = 0;
    }
    else{
        throttle = left.read() * 100;
    }
}

void press(){
    if(throttle < 100) {
        throttle = 100;
    }
    else {
        throttle = left.read();
    }
}
void release(){
    throttle = left.read();
}

bool ispressed(int b) {
  return (buttons[b].sw.read())^buttons[b].invert;
}

/* States from Lander */
SocketAddress lander("192.168.80.5",65200);
SocketAddress dash("192.168.80.5",65250);

EthernetInterface eth;
UDPSocket udp;

/* Synchronous UDP communications with lander */
void communications(void){
    SocketAddress source;

    char buffer[512];
    sprintf(buffer,"command:!\nthrottle: %d\nroll:%.2f\n", throttle, roll);
    printf(buffer);

    udp.sendto( lander, buffer, strlen(buffer));
    nsapi_size_or_error_t n = udp.recvfrom(&source, buffer, sizeof(buffer));
    buffer[n] = '\0';

    /* Unpack incomming message */
        char *nextline, *line;
    for(
    line = strtok_r(buffer, "\r\n", &nextline);
    line != NULL;
    line = strtok_r(NULL, "\r\n", &nextline)
    ) {
      char *key, *value;
      key = strtok(line, ":");
      value = strtok(NULL, ":");
      if( strcmp(key,"altitude")==0 ) {
          altitude = atof(value);
      }
      else if( strcmp(key,"fuel")==0 ) {
          fuel = atoi(value);
      }
      else if( strcmp(key,"flying")==0 ) {
          flying = atoi(value);
      }
      else if( strcmp(key,"crashed")==0 ) {
          crashed = atoi(value);
      }
      else if( strcmp(key,"Vx")==0 ) {
          vx = atof(value);
      }
      else if( strcmp(key,"Vy")==0 ) {
          vy = atof(value);
      }
    }

    if(crashed == 0 && flying == 0) {
        landed = 1;
    }

    if(vx < 10 && vy < 10) {
        landable = 1;
    }
    else {
        landable = 0;
    }

}

/* Asynchronous UDP communications with dashboard */
void dashboard(void){
    /* message to the Dashboard */
    char buffer[512];
    sprintf(buffer,"throttle:%d\nroll:%.2f\naltitude:%.2f\nfuel:%d\nflying:%d\ncrashed:%d\nVx:%.2f\nVy:%.2f\nlanded:%d\nlandable:%d", throttle, roll, altitude, fuel, flying, crashed, vx, vy, landed, landable);
        udp.sendto( dash, buffer, strlen(buffer));
}

int main() {
    acc.enable();

    printf("connecting \n");
    eth.connect();
    /* write obtained IP address to serial monitor */
    const char *ip = eth.get_ip_address();
    printf("IP address is: %s\n", ip ? ip : "No IP");

    /* open udp for communications on the ethernet */
    udp.open( &eth);

    printf("lander is on %s/%d\n",lander.get_ip_address(),lander.get_port() );
    printf("dash   is on %s/%d\n",dash.get_ip_address(),dash.get_port() );

    /* periodic tasks */

    periodic.call_every(20, user_input);
    periodic.call_every(20, communications);
    periodic.call_every(100, dashboard);


    sw2.fall(periodic.event(press));


    /* start event dispatching thread */
    dispatch.start( callback(&periodic, &EventQueue::dispatch_forever) );

    while(1) {
        /* update display at whatever rate is possible */
        if (crashed == 1){
            playCrash();
            green.write(1);
            red.write(0);
            lcd.cls();
            lcd.locate(0, 0);
            lcd.printf("YOU CRASHED, GAME OVER");

            red.write(0);
        }
        else if (landed == 1) {
            playLand();
            green.write(0);
            red.write(1);
            lcd.cls();
            lcd.locate(0, 0);
            lcd.printf("YOU LANDED!");
        }
        else{
        /* Show user information on the LCD */
            speaker.write(0);
            red.write(1);
            green.write(1);
            lcd.locate(0, 0);
            lcd.printf("Throttle : %d | Fuel : %d", throttle, fuel);
            lcd.printf("\nRoll : %.2f", roll);
            if(vx < 10 && vy < 10) {
                lcd.printf("\nVelocity: LANDABLE");
            }
            else{
                lcd.printf("\nVelocity: TOO FAST");
            }
        }
        wait(1);

    }
}
