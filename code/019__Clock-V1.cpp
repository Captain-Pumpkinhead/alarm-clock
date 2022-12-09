#include <stdio.h>
#include <cmath>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/gpio.h"
#include "hardware/adc.h"

//For printing numbers in binary.  Source: https://stackoverflow.com/questions/111928/is-there-a-printf-converter-to-print-in-binary-format
#define BYTE_TO_BINARY_PATTERN "%c%c%c%c%c%c%c%c"
#define BYTE_TO_BINARY(byte)  \
  (byte & 0x80 ? '1' : '0'), \
  (byte & 0x40 ? '1' : '0'), \
  (byte & 0x20 ? '1' : '0'), \
  (byte & 0x10 ? '1' : '0'), \
  (byte & 0x08 ? '1' : '0'), \
  (byte & 0x04 ? '1' : '0'), \
  (byte & 0x02 ? '1' : '0'), \
  (byte & 0x01 ? '1' : '0') 

/**
 * @brief This is the code for my V1 of the pixel sunrise clock.  Right now
 * it has no pixels, only sunrise.
 * 
 * Minimum Viable Product:
 *  - The seven segment displays iterate through a 24 hour clock.
 *  - There is a way to set the clock time.
 *  - The clock keeps track of what weekday it is.
 *  - There is one alarm that can be configured to go off on whatever weekday you select,
 *    and whatever time you select.
 *  - The alarm sets off both an audio response and an LED response.
 * 
 * Roadmark 2:
 *  - Able to set multiple alarms
 *  - Keeps track of Unix time
 *  - Keeps track of time in a manner more efficient than my current core-swallowing method.
 *    This may mean some built-in timekeeping function, or it may mean some sort of clever
 *    PIO manipulation.
 *  - Able to play songs as alarm.
 *  - RGB compatibility for alarms and music playing
 * 
 * Roadmark 3:
 *  - Some sort of web interface to set up alarms, timedate, etc.  Maybe Gradio based?
 *  - Set up the web interface to be easy to connect to.  Some sort of consistent URL 
 *    or IP address or something?
 *  - Use MP3 files instead of embedding the audio into the UF2.
 *  - Maybe some early pixel integration?
 */

void second_core()
{
    /**
     * Plan for second_core():
     * 
     * Second core waits for 1000 ms, adds one to the Unix time,
     * and then pushes it to the first core.
     * This can be optimized greatly by breaking down how the
     * "sleep()" function works, but for now, this is just making
     * it work at all.  Perhaps could be further optimized via PIO.
    */
   
    while(true)
    {
        sleep_ms(1000);
        multicore_fifo_push_blocking(true);
        //There must be a better way of doing this.
        //But that's okay.  Let's just build something that works,
        //and then focus on fixing it later.
    }
};

class Clock
{
    class SoundNode
    {
        public: //TODO: Figure out what here needs to be public vs private
            bool loop;
            SoundNode previous; //Seems that an instance variable of a class is done differently in C++ than in Java.
            SoundNode next;
            uint8_t volume = 100;
            double step = 0;
            double speed = 1;
            uint32_t length;
            
            /**
             * Alright, plan to fix up the "enumConstantAnalogValuesThingy" problem:
             * 
             * I'm gonna store songs like this until I understand how to write and read files better.  I think how
             * I want to do this is this:
             * 1. I have an enum with names of songs or sounds.
             * 2. Creation of an instance specifies an enum of a song or sound.
             * 3. Constructor asks for the enum, and writes the sound's analog values to RAM.
             * 
             * This way the values are stored on disk until needed, and aren't hogging RAM unnecessarilly.  It's not
             * perfect, but it's acceptable.
             */
            double enumConstantAnalogValuesThingy;
            //TODO: Right here there needs to be a variable that points to a constant.  That constant should be the enum
            //values.  Those enum values should be the analog values of the song or sound.
            //Now set up an enum that contains all the songs or whatever

            SoundNode(uint8_t song_selection)
            {
                //TODO: This is more psuedo-code than real code.  Fix it.
                enumConstantAnalogValuesThingy = song_selection;
                length = enumConstantAnalogValuesThingy.length;
            }

            double play()
            {
                step += speed;
                if (step != (int) step)
                {
                    //If step is not an integer, then do a y=mx+b to get the exact value it should be.
                    // m = (y2 - y1)/(x2 - x1)
                    double m = enumConstantAnalogValuesThingy[(int) ceil(step)] - enumConstantAnalogValuesThingy[(int) floor(step)];
                    return enumConstantAnalogValuesThingy[(int) floor(step)] + (step - floor(step)) * m;
                }
                else
                    return enumConstantAnalogValuesThingy[(int) step];
            }

            SoundNode get_next()
            {
                return next;
            }
    };

    //GPIO ///////////////////////////////  
    const uint SEG7A     =  1;          //  These are the anode pins of the seven-segment
    const uint SEG7B     =  0;          //  displays.  Anodes are paires with the same anode
    const uint SEG7C     =  6;          //  of neighboring displays, cathodes are kept seperate.
    const uint SEG7D     =  8;          //  
    const uint SEG7E     =  7;          //  
    const uint SEG7F     =  3;          //  
    const uint SEG7G     =  2;          //  
    const uint SEG7DP    = 10;          //  
    const uint SEG7GND[4]= {4,9,14,15}; //  These are the cathodes of seven-segment displays.
                                        //  Activate each one individually to control showing
                                        //  different characters on each display.
    const uint SPEAKER   = 16;          //  Pin for the speakers.
    const uint LED       = 17;          //  Pin for the LED "sunrise" lights.
    const uint LED_PICO  = 25;          //  The Pico's onboard LED.
    const uint PHOTORST  = 26;          //  Pin for the photoresistor measurement.
    const uint BUTTON[8] = {18, 19, 20, //  Button 0 for the light.  Maybe multipurpose?
                    21, 64, 64, 64, 64};//  Button 1 for Minus, Button 2 for Plus, Button 3 for Alarm.
    const uint SUNRISE   = 64;          //  Temp value.  GPIO for turning on sunrise-simulating lamp.
    //System Variables////////////////////  
    //uint16_t adc_ldr;             //  Holds the value read by the ADC at the photoresistor pin.
    double step = 0;                //  The step along the sine wave
    double fill = 0;                //  The fill  is a measure of whether to output or not.
    bool button_pressed[8];         //  
    uint32_t button_held[8] = {0,0,0,0,0,0,0,0};            //  TODO: Check online what the right way to do this is.
    uint8_t function_select = 0;   //  
    uint16_t time = 0;
    
    uint8_t display[4];                 //  Stores the binary codes for which seven segment display LEDs to turn on.  Each index represents one of the four digits on the device.  The values may be stored as uint, but they're converted into 8 bit binary values upon usage.  This could also be accomplished with a 4x8 boolean array.  That might be smarter.
    uint8_t display_delay = 16;         //  The number of microseconds to pause on a digit before moving on.  Can work with as low as 1 microsecond, but it needs *some* kind of delay in order to look right.  Going as high as ~5 milliseconds is undesireable.  Don't remember why, sorry.  I think you start seeing flickering at 8 milliseconds or so.
    double brightness;                  //  The brightness of the environment, as measured by the photoresistor.  Used to determine how brightly the seven segment LEDs should shine.
    double bright_fill = 0;             //  This is a PWM variable.  Brightness gets added to it.  If bright_fill is >= 1, then the LEDs turn on (via bright_OnOff) and the variable -= 1.  Repetition turns it on and off bright enough for the human eye to see it as differing brightness levels instead of as flickering/flashing.
    bool bright_OnOff = 0;              //  Determines whether the seven segment displays get turned on or not.  Determined by bright_fill.
    bool sunrise_OnOff = false;         //  Whether the sunrise light is turned on.
    uint8_t sunrise_target_machine = 1; //  The machine-set brightness of the sunrise light.  Differentiated for alarm reasons and such.  Will usually be the same as sunrise_target_user, though.
    uint8_t sunrise_target_user = 1;    //  The user-set brightness of the sunrise light.  Max 10? 20?  255?
    double sunrise_brightness = 1;      //  The actual brightness of the sunrise light.  Used for manipulating slow light up & such.
    double sunrise_fill = 0;            //  The PWM "fill" variable of the sunrise light.
    double sound = 1;                   //  
    double sound_sine_step = 0;         //  
    double sound_sine_interval = 0.004; //  
    double sound_fill = 0;              //  
    bool sound_OnOff = 0;               //  
    long sound_start = -1;              //  
    SoundNode sound_node_first;         //  
    
    uint8_t year = 2022;
    uint8_t month = 0;
    uint8_t day = 0;
    uint8_t weekday = 0;
    uint8_t hour = 10;
    uint8_t minute = 40;
    uint8_t second = 0;
    //////////////////////////////////  
    
    //Seven Segment Character Array///////  
    uint8_t charDisplay[128] = {        //  ASCII characters translated to seven segment
        63,     // #48, '0', 00111111   //  display codes.  The numbers assigned represent
        6,      // #49, '1', 00000110   //  binary on-off states for the eight LEDs on the
        91,     // #50, '2', 01011011   //  seven segment displays.  With the exception of
        79,     // #51, '3', 01001111   //  the first 10 values (reassigned) and any
        102,    // #52, '4', 01100110   //  characters which will not be used (assigned as
        109,    // #53, '5', 01101101   //  zero), all characters are in the same index
        125,    // #54, '6', 01111101   //  position as the standard ASCII chart.
        7,      // #55, '7', 00000111   //  
        127,    // #56, '8', 01111111   //  The first 30 or so characters aren't useful,
        111,    // #57, '9', 01101111   //  so I reassigned indeces 0-9 to be the digits 0-9.
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0,   //  This cleans up the math significantly.
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0,   //  
        0, 0,                           //  
        0,      // #32, ' ', 00000000   //  
        0, 0, 0, 0, 0, 0,               //  
        2,      // #39, "'", 00000010   //  Just to be a little more clear about how this
        57,     // #40, '(', 00111001   //  is laid out:
        15,     // #41, ')', 00001111   //  
        0, 0, 0,                        //      The first number, the one that's actually
        64,     // #45, '-', 01000000   //  going into the array, is the binary number put
        128,    // #46, '.', 10000000   //  into decimal form.  I would love to write it out
        0,                              //  in binary form since it's being used in binary
        63,     // #48, '0', 00111111   //  form, but I don't understand how to do that.
        6,      // #49, '1', 00000110   //      The second number, the one that's immediately
        91,     // #50, '2', 01011011   //  behind the comment slashes, is labeling the index.
        79,     // #51, '3', 01001111   //  It's just for convenience, really.
        102,    // #52, '4', 01100110   //      The next section is the character in apostrophes.
        109,    // #53, '5', 01101101   //      The last section is the binary for the value.
        125,    // #54, '6', 01111101   //      And if a character is unused or unusable, then
        7,      // #55, '7', 00000111   //  I have simply put a zero there with no comment
        127,    // #56, '8', 01111111   //  labeling.  I think it's worth saving the vertical
        111,    // #57, '9', 01101111   //  space.
        0, 0, 0, 0, 0, 0, 0,            //  
        119,    // #65, 'A', 01110111   //      The way this is set up is really convenient
        127,    // #66, 'B', 01111111   //  because you can just use a CHAR as the INT index
        57,     // #67, 'C', 00111001   //  of the array.  No need to think it through, just
        127,    // #68, 'D', 01111111   //  give it the char and it'll spit out the correct
        121,    // #69, 'E', 01111001   //  code for the display.
        113,    // #70, 'F', 01110001   //  
        61,     // #71, 'G', 00111101   //  
        118,    // #72, 'H', 01110110   //  
        6,      // #73, 'I', 00000110   //  
        30,     // #74, 'J', 00011110   //  
        128,    // #75, 'K', 10000000   //  
        56,     // #76, 'L', 00111000   //  
        128,    // #77, 'M', 10000000   //  
        55,     // #78, 'N', 00110111   //  
        63,     // #79, 'O', 00111111   //  
        115,    // #80, 'P', 01110011   //  
        128,    // #81, 'Q', 10000000   //  
        128,    // #82, 'R', 10000000   //  
        109,    // #83, 'S', 01101101   //  
        128,    // #84, 'T', 10000000   //  
        62,     // #85, 'U', 00111110   //  
        128,    // #86, 'V', 10000000   //  
        128,    // #87, 'W', 10000000   //  
        128,    // #88, 'X', 10000000   //  
        128,    // #89, 'Y', 10000000   //  
        128,    // #90, 'Z', 10000000   //  
        0, 0, 0, 0,                     //
        8,      // #95, '_', 00001000   //  
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0,   //  
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0,   //
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0,   //  
        0, 0                            //  
    };                                  //  
    //////////////////////////////////////  
    public:
        void display_digit(uint8_t digit_select)
        {/// TURN ON ONE OF THE DISPLAY DIGITS ///////////////////////////////////  
            gpio_put(SEG7GND[0], 1);                                            //      The function begins by assuming all
            gpio_put(SEG7GND[1], 1);                                            //  digits and segments might be turned on.
            gpio_put(SEG7GND[2], 1);                                            //  They shouldn't be, but they might. The
            gpio_put(SEG7GND[3], 1);                                            //  SEG7GND[n] assignments ensure that the
            gpio_put(SEG7A,  0);                                                //  digits are turned off, the rest ensure
            gpio_put(SEG7B,  0);                                                //  the segments are turned off.
            gpio_put(SEG7C,  0);                                                //      Which digit to display is stored
            gpio_put(SEG7D,  0);                                                //  in the display[n] array.  This way I
            gpio_put(SEG7E,  0);                                                //  only need one parameter, not two. 
            gpio_put(SEG7F,  0);                                                //  Possibly not the best option long term.
            gpio_put(SEG7G,  0);                                                //  But for now, it works just fine.
            gpio_put(SEG7DP, 0);                                                //      Each binary digit of the decimal
            gpio_put(SEG7GND[digit_select], 0);                                 //  number is extracted and weighed against
            gpio_put(SEG7A,  (display[digit_select] & 0x01 ? bright_OnOff : 0));//  its respective LED.  If TRUE, then
            gpio_put(SEG7B,  (display[digit_select] & 0x02 ? bright_OnOff : 0));//  bright_OnOff tells the LED to turn on
            gpio_put(SEG7C,  (display[digit_select] & 0x04 ? bright_OnOff : 0));//  or not.  ELSE it stays off.
            gpio_put(SEG7D,  (display[digit_select] & 0x08 ? bright_OnOff : 0));//      Delay at the end is to trick the
            gpio_put(SEG7E,  (display[digit_select] & 0x10 ? bright_OnOff : 0));//  human eye.  A few microseconds of delay
            gpio_put(SEG7F,  (display[digit_select] & 0x20 ? bright_OnOff : 0));//  makes it look like the light is 
            gpio_put(SEG7G,  (display[digit_select] & 0x40 ? bright_OnOff : 0));//  consistent instead of alternating.
            gpio_put(SEG7DP, (display[digit_select] & 0x80 ? bright_OnOff : 0));//      I'd like to add a feature to leave
            sleep_us(display_delay);                                            //  the digit's decimal point on or not.
        }/////////////////////////////////////////////////////////////////////////  
        
        void button(uint8_t button_select, bool pressed)  //  Light
        {
            /**
             * This whole function is an eyesore, and I'm not sure how to make it better.
             * 
             * This function is built on three different levels:
             * 1. Which button is being pressed?
             *     2. Which button mode is the clock in?  For example, normal operation, 
             *        or setting the time, or testing functionality.
             *         3. Is the button being pressed or released?
             * 
             *     This is the smartest and most compact way of setting it up that I could
             * think of.  But it's horrendous to look at, and takes up so much vertical
             * space.  Technically I could make it smaller by deleting the unused sections,
             * but I like keeping them there so that the flow of options is obvious.
             */
            switch (button_select)
            {
            case 0: //Button 0 - Sunrise Light
                switch (function_select)   //Button Modes
                {
                case 1: //  Testing sound functions
                    if (pressed)    //  For when the button is pressed/held down
                    {
                        sound_sine_out();
                    }
                    else    //For when the button is released
                    {}
                    break;
                
                case 2: //  
                    if (pressed)    //  For when the button is pressed/held down
                    {}
                    else    //For when the button is released
                    {
                        time *= 1.15;
                        if(time >= 10000)
                            time = 0;
                        printf("\n\ntime: %d",time);
                    }
                    break;
                
                default://  Probably how it runs most of the time?
                    if (pressed)    //  For when the button is pressed/held down
                    {}
                    else    //For when the button is released
                    {
                        sunrise_OnOff = !sunrise_OnOff;
                        if (sunrise_OnOff)
                            sunrise_target_machine = sunrise_target_user
                        else
                            sunrise_target_machine = 0;
                    }
                    break;
                }
                break;
            case 1: //Button 1 - Minus Button
                switch (function_select)
                {
                case 1: //  Testing sound functions
                    if (pressed)    //  For when the button is pressed/held down
                    {}
                    else    //For when the button is released
                    {
                        sound_sine_interval *= 2;
                        if(sound_sine_interval == 0)
                            sound_sine_interval = 1;
                        printf("\n\nsound_sine_interval: %f",sound_sine_interval);
                    }
                    break;
                
                default://  Probably how it runs most of the time?
                    if (pressed)    //  For when the button is pressed/held down
                    {
                        if (sunrise_OnOff && sunrise_target_user > 0)
                        {
                            button_held[1] += 1;
                            sunrise_target_user -= floor( (button_held[1]%(16/floor(log(button_held[1])))) / floor((16/floor(log10(button_held[1])))-1));
                            sunrise_target_machine = sunrise_target_user;
                            /**
                             * The formula:
                             * 
                             * floor(    (   sin(  button_held[1]^2  )+1  )/2  )
                             * 
                             * is intended to make the number increase start slow, and 
                             * then pick up speed the longer it is held.
                             * 
                             * HOWEVER!
                             * 
                             * It is untested in an integer setting.
                             * And I'll need to determine whether radians or degrees
                             * will suit this formula best.
                             * And I'll need to test whether the speed and speed ramp
                             * are what I want them to be.
                             * 
                             * 
                             * EDIT:
                             * 
                             * I've tested it in an integer environment, and it doesn't
                             * work.  It is on the right track, though.
                             * 
                             * EDIT 2:
                             * Here's the new formula:
                             * 
                             * floor( (x%(16/floor(log(x)))) / floor((16/floor(log10(x)))-1))
                            */
                        }
                    }
                    else    //For when the button is released
                    {
                        button_held[1] = 0;
                        if (sunrise_OnOff && sunrise_target_user > 0)
                        {
                            sunrise_target_user -= 1;
                            sunrise_target_machine = sunrise_target_user;
                        }
                    }
                    break;
                }
                break;
            case 2: //Button 2 - Plus Button
                switch (function_select)
                {
                case 1: //  Testing sound functions
                    if (pressed)    //  For when the button is pressed/held down
                    {}
                    else    //For when the button is released
                    {
                        sound_sine_interval /= 4;
                        printf("\n\nsound_sine_interval: %f",sound_sine_interval);
                    }
                    break;
                
                default://  Probably how it runs most of the time?
                    if (pressed)    //  For when the button is pressed/held down
                    {
                        if (sunrise_OnOff && sunrise_target_user < 255)
                        {
                            button_held[2] += 1;
                            sunrise_target_user += floor( (button_held[2]%(16/floor(log(button_held[2])))) / floor((16/floor(log10(button_held[2])))-1));
                            sunrise_target_machine = sunrise_target_user;
                        }
                    }
                    else    //For when the button is released
                    {
                        button_held[2] = 0;
                        if (sunrise_OnOff && sunrise_target_user < 255)
                        {
                            sunrise_target_user += 1;
                            sunrise_target_machine = sunrise_target_user;
                        }
                    }
                    break;
                }
                break;
            case 3: //Button 3 - Alarm
                switch (function_select)
                {
                case 1: //  
                    if (pressed)    //  For when the button is pressed/held down
                    {}
                    else    //For when the button is released
                    {}
                    break;
                
                default://  Probably how it runs most of the time?
                    if (pressed)    //  For when the button is pressed/held down
                    {}
                    else    //For when the button is released
                    {}
                    break;
                }
                break;
            case 4: //Button 4 - Clock Set
                switch (function_select)
                {
                case 1: //  
                    if (pressed)    //  For when the button is pressed/held down
                    {}
                    else    //For when the button is released
                    {}
                    break;
                
                default://  Probably how it runs most of the time?
                    if (pressed)    //  For when the button is pressed/held down
                    {}
                    else    //For when the button is released
                    {}
                    break;
                }
                break;
            case 5: //Button 5 - Left?   If used.
                switch (function_select)
                {
                case 1: //  
                    if (pressed)    //  For when the button is pressed/held down
                    {}
                    else    //For when the button is released
                    {}
                    break;
                
                default://  How it runs most of the time.
                    if (pressed)    //  For when the button is pressed/held down
                    {
                        
                    }
                    else    //For when the button is released
                    {
                        
                    }
                    break;
                }
                break;
            case 6: //Button 6 - Right?  If used.
                switch (function_select)
                {
                case 1: //  
                    if (pressed)    //  For when the button is pressed/held down
                    {
                        
                    }
                    else    //For when the button is released
                    {
                        
                    }
                    break;
                
                default://  Probably how it runs most of the time?
                    if (pressed)    //  For when the button is pressed/held down
                    {
                        
                    }
                    else    //For when the button is released
                    {
                        
                    }
                    break;
                }
                break;
            case 7: //Button 7 - Reserved.  Can't really think of anything useful right now.
                switch (function_select)
                {
                case 1: //  
                    if (pressed)    //  For when the button is pressed/held down
                    {
                        
                    }
                    else    //For when the button is released
                    {
                        
                    }
                    break;
                
                default://  Probably how it runs most of the time?
                    if (pressed)    //  For when the button is pressed/held down
                    {
                        
                    }
                    else    //For when the button is released
                    {
                        
                    }
                    break;
                }
                break;
            
            }
        }
        
        
        
        
        
        
        
        void set_time()
        {

        }
        
        void sound_sine_out()
        {
            sound_fill += 1 - (sin(sound_sine_step)/2 + 0.5);
            if (sound_fill >= 1)
            {
                sound_fill -= 1;
                gpio_put(SPEAKER, 0);
            }
            else
                gpio_put(SPEAKER, 1);
            sound_sine_step += sound_sine_interval;
        }
        
        
        {
            /**
             * Idea:
             *      Utilize a FIFO and the other core to get sound out.
             * sound_out() only handles the sound_fill, the gpio_put(SPEAKER,n),
             * and popping the FIFO to get the next sound step.
             *      Alternatively, sound_out() doesn't even handle the sound_fill,
             * that could be handled by the second core.
             * 
             * But we'll check the feasibility of that later.  For now, let's just
             * make something that works.
            */

            uint8_t node_count = 0;
            SoundNode current_node = sound_node_first;
            while (current_node != NULL)
            {
                node_count +=1;
                current_node = current_node.get_next();
            }

            double analog_values[node_count];
            node_count = 0;
            current_node = sound_node_first;
            while (current_node != NULL)
            {
                analog_values[node_count] = current_node.play();
                node_count +=1;
                current_node = current_node.get_next();
            }
            
            /**
             * This one's gonna need some testing & stuff.  Don't know what the best way of doing this is gonna be.
             * Basically I need to figure out how I'm gonna mix multiple sounds together, and how I'm gonna return
             * the values.
             * 
             * Idea 1: Multiply each value by the square root of each other.  More likely, it will need to be 
             *          the nth-root, with n being the number of sounds being played.
             * Idea 2: Perform a mean average.
             * Idea 3: Multiply each value across each other with poles of +1 and -1.  Probably not a great solution.
            */

            double output;
            switch (1)
            {
            case 1:
                output = 1;
                for (size_t i = 0; i < node_count; i++)
                {
                    output *= sqrt(analog_values[i]);
                }
                break;
            case 2:
                output = 1;
                for (size_t i = 0; i < node_count; i++)
                {
                    output *= nthroot(analog_values[i], node_count);
                }
                break;
            case 3:
                output = 0;
                for (size_t i = 0; i < node_count; i++)
                {
                    output += analog_values[i];
                }
                output/node_count;
                break;
            case 4:
                output = 1;
                for (size_t i = 0; i < node_count; i++)
                {
                    output *= analog_values[i];
                }
                break;
            case 5:
                output = 1;
                for (size_t i = 0; i < node_count; i++)
                {
                    output *= analog_values[i];
                }
                break;
            default:
                output = 1;
                for (size_t i = 0; i < node_count; i++)
                {
                    output *= nthroot(sin(analog_values[i]), node_count);
                }
                break;
            }

            sound_fill += output;
            if (sound_fill >= 1)
            {
                sound_fill -= 1;
                gpio_put(SPEAKER, 1);
            }
            else
                gpio_put(SPEAKER, 0);
        }
        
        SoundNode start_sound(int8_t type, double selection, uint8_t volume)
        {
            if (sound_node_first == null)
            {
                sound_node_first = new SoundNode(type, select, volume);
            }
            else
            {
                SoundNode current_node = sound_node_first;
                while (current_node.get_next() != null)
                    current_node = current_node.get_next();
                current_node.next = new SoundNode(type, select, volume);
                current_node.next.previous = current_node;
            }
        }
        
        int main()
        {
            //Initialization//////////////////////////  
            stdio_init_all();                       //  This allows me to perform serial output.  CMakeLists has that set to USB.
            adc_init();                             //  Enable the Analog to Digital Converter before I initialize ADC pins.
            adc_select_input(0);                    //  Sets ADC to GPIO 26, our LDR pin.
                                                    //  
            multicore_launch_core1(second_core);    //
                                                    //  
            // GPIO Initialization ///////////////////
            gpio_init(SEG7A);                   //  
            gpio_init(SEG7B);                   //  
            gpio_init(SEG7C);                   //  
            gpio_init(SEG7D);                   //  
            gpio_init(SEG7E);                   //  
            gpio_init(SEG7F);                   //  
            gpio_init(SEG7G);                   //  
            gpio_init(SEG7DP);                  //  
            gpio_init(SEG7GND[0]);              //  
            gpio_init(SEG7GND[1]);              //  
            gpio_init(SEG7GND[2]);              //  
            gpio_init(SEG7GND[3]);              //  
            gpio_init(SPEAKER);                 //  
            gpio_init(LED);                     //  
            gpio_init(LED_PICO);                //  
            adc_gpio_init(PHOTORST);            //  Measures the power of Light Dependendant Resistor, aka Photoresistor
            gpio_init(BUTTON[0]);               //  
            gpio_init(BUTTON[1]);               //  
            gpio_init(BUTTON[2]);               //  
            gpio_init(BUTTON[3]);               //  
            gpio_set_dir(SEG7A,    GPIO_OUT);   //  
            gpio_set_dir(SEG7B,    GPIO_OUT);   //  
            gpio_set_dir(SEG7C,    GPIO_OUT);   //  
            gpio_set_dir(SEG7D,    GPIO_OUT);   //  
            gpio_set_dir(SEG7E,    GPIO_OUT);   //  
            gpio_set_dir(SEG7F,    GPIO_OUT);   //  
            gpio_set_dir(SEG7G,    GPIO_OUT);   //  
            gpio_set_dir(SEG7DP,   GPIO_OUT);   //  
            gpio_set_dir(SEG7GND[0], GPIO_OUT); //  
            gpio_set_dir(SEG7GND[1], GPIO_OUT); //  
            gpio_set_dir(SEG7GND[2], GPIO_OUT); //  
            gpio_set_dir(SEG7GND[3], GPIO_OUT); //  
            gpio_set_dir(SPEAKER,  GPIO_OUT);   //  
            gpio_set_dir(LED,      GPIO_OUT);   //  
            gpio_set_dir(LED_PICO, GPIO_OUT);   //  
            //gpio_set_dir(PHOTORST, GPIO_IN ); //  Unnecessary because of ADC?  Might possibly interfere?
            gpio_set_dir(BUTTON[0], GPIO_IN );  //  
            gpio_set_dir(BUTTON[1], GPIO_IN );  //  
            gpio_set_dir(BUTTON[2], GPIO_IN );  //  
            gpio_set_dir(BUTTON[3], GPIO_IN );  //  
            gpio_pull_up(BUTTON[0]);            //   
            gpio_pull_up(BUTTON[1]);            //   
            gpio_pull_up(BUTTON[2]);            //   
            gpio_pull_up(BUTTON[3]);            //   
            //////////////////////////////////////


            //Code Infinite Loop//////////////////////////  
            while (true)                                //  
            {   //multicore_fifo_push_blocking(1);      //  Send a message to Core1 requesting data
                //step = (double)multicore_fifo_pop_blocking()/1000000;   //  Retrieve the step, sine, and fill for outputting
                //sine = (double)multicore_fifo_pop_blocking()/1000000;   //  to PuTTY.
                //fill = (double)multicore_fifo_pop_blocking()/1000000;   //  
                                                        //  
                //////////////////////////////////////////
                                                        //                  Serial via USB Output
                brightness = adc_read()/3500.0;
                //printf("Photoresistor: %d\nBrightness: %f\n\n",adc_ldr,brightness);  //  
                bright_fill += brightness;
                if (bright_fill >= 1)
                {
                    bright_fill -= 1;
                    bright_OnOff = 1;
                }
                else
                    bright_OnOff = 0;
                
                
                //  Button activation.
                //  If it's pressed, call the function for it being pressed.
                //  If it's released, call the function for it being released.
                for (size_t i = 0; i < 8; i++)
                {
                    if(!gpio_get(BUTTON[i]))
                    {
                        button(i, true);
                        button_pressed[i] = true;
                    }
                    else if(button_pressed[i] == true)
                    {
                        button(i, false);
                        button_pressed[i] = false;
                    }
                }
                
                
                
                
                
                
                display[0] = charDisplay[(int) floor(hour  /10)];
                display[1] = charDisplay[hour   - (int) floor(hour  /10)*10];
                display[2] = charDisplay[(int) floor(minute/10)];
                display[3] = charDisplay[minute - (int) floor(minute/10)*10];
                
                for (size_t i = 0; i < 4; i++)
                {
                    display_digit(i);
                }
                gpio_put(SEG7DP, (second % 2)*bright_OnOff);
                
                //Sunrise brightness
                if (sunrise_brightness != sunrise_target_machine)
                {
                    sunrise_brightness += sqrt(sunrise_target_machine - sunrise_brightness);
                    //sunrise_brightness += 0.01 * (sunrise_target_machine - sunrise_brightness);
                    //sunrise_brightness += (sunrise_target_machine - sunrise_brightness);
                }
                sunrise_fill += sunrise_brightness/255.0;
                if (sunrise_fill >= 1)
                {
                    sunrise_fill -= 1;
                    gpio_put(SUNRISE, 1);
                }
                else
                    gpio_put(SUNRISE, 0);
                
                
                
                if(multicore_fifo_rvalid())
                {
                    multicore_fifo_pop_blocking();
                    second += 1;
                    if (second >= 60)
                    {
                        second -= 60;
                        minute += 1;
                        if (minute >= 60)
                        {
                            minute -= 60;
                            hour += 1;
                            if (hour >= 24)
                            {
                                hour -= 24;
                                day += 1;
                                /*  Gotta implement a DAYS_PER_MONTH[month] system.
                                if (day >= DAYS_PER_MONTH[month])
                                {
                                    day -= DAYS_PER_MONTH[month];
                                    month += 1;
                                    if (month >= 12)
                                    {
                                        month -= 12;
                                        year += 1;
                                    }
                                }*/
                                weekday += 1;
                                if (weekday >= 7)
                                {
                                    weekday -= 7;
                                }
                            }
                        }
                    }
                }
            }                                           //  
            //////////////////////////////////////////////  
        }//End main()

};




int main()
{
    Clock clock;
    clock.main();
}