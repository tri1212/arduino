const int CLK_BURST_READ_REG  = 0xBF; // Clock Burst read register
const int CLK_BURST_WRITE_REG = 0xBE; // Clock Burst write register

const int WP_REG = 0x8E; // Write-Protect register
const int WP_BIT = 0x07; // Write-Protect bit

const int CH_REG = 0x80; // Clock Halt flag register
const int CH_BIT = 0x07; // Clock Halt bit
  
const int RTC_RST_PIN = PC3; // RST/CE pin
const int RTC_IO_PIN  = PC4; // DATA/IO pin
const int RTC_CLK_PIN = PC5; // CLOCK pin

const int DEBOUNCE_TIME = 300; // Button debounce time in Ms

int SEGMENT_PINS[7] = {PD2, PD3, PD4, PD5, PD6, PD7, PB0};
int DIGIT_PINS[4]   = {PB1, PB2, PB3, PB4};
int BUTTON_PINS[4]  = {PC0, PC1, PC2};

// For setting the time
int digit0 = 0;
int digit1 = 0;
int digit2 = 0;
int digit3 = 0;
int selected_digit = 0;

// For keeping the time
int hour = 0;
int min  = 0;
int sec  = 0;

unsigned long currMillis = 0;
unsigned long prevMillis = 0;
unsigned long currBlinkMillis = 0;
unsigned long prevBlinkMillis = 0;
bool blink_state = false;

volatile bool one_sec_elapsed = false;

enum display_mode{SET, TIME, IDLE};
display_mode mode = IDLE; // Default mode

// 7 digit display patterns(common anode)
int number[10][7] = 
{
  {0,0,0,0,0,0,1},
  {1,0,0,1,1,1,1},
  {0,0,1,0,0,1,0},
  {0,0,0,0,1,1,0},
  {1,0,0,1,1,0,0},
  {0,1,0,0,1,0,0},
  {0,1,0,0,0,0,0},
  {0,0,0,1,1,1,1},
  {0,0,0,0,0,0,0},
  {0,0,0,0,1,0,0}
};

void setup ()
{ 
  Serial.begin(9600);

  // Pins setup
  DDRD |= B11111100;
  DDRB |= B00011111;
  DDRC |= B00111000;
  
  // Reset registers
  TCCR1A = 0; 
  TCCR1B = 0;
  TCNT1 = 0;
  
  OCR1A = 15624; // 1 second

  TCCR1B |= (1 << WGM12);  // CTC mode on
  TCCR1B |= (1 << CS10) | (1 << CS12); // 1024 prescaler
  TIMSK1 |= (1 << OCIE1A);  // Enable timer1 compare interrupt

  // Clear the Write-Protect bit and Clock Halt flag
  rtc_stop(false);

  sei();
}

void loop ()
{
  currMillis = millis();
  bool buttonPressed[3];
  for(int i = 0; i < 3; i++) // Debounce buttons
  {
    if((PINC & (1 << BUTTON_PINS[i])) && currMillis - prevMillis > DEBOUNCE_TIME)
    {
      prevMillis = currMillis;
      buttonPressed[i] = true;
    }
    else buttonPressed[i] = false;
  }
  
  // Change mode
  if(buttonPressed[0])
  {
    switch(mode)
    {
      case TIME: // If the current mode is TIME or IDLE, change to SET
      case IDLE:
        mode = SET;      
        digit0 = hour / 10;
        digit1 = hour % 10;
        digit2 = min / 10;
        digit3 = min % 10;
        selected_digit = 0;
        TIMSK1 = 0; // Disable timer1 interrupts
        rtc_stop(true); // Stop the RTC
        break;
        
      case SET: // If the current mode is SET, change to TIME
        mode = TIME;       
        hour = digit0 * 10 + digit1;
        min = digit2 * 10 + digit3;
        one_sec_elapsed = false;
        TCNT1 = 0;
        TIMSK1 |= (1 << OCIE1A);  // Enable timer1 compare interrupt
        rtc_stop(false); // Start the RTC before setting the time
        rtc_set_time(1, min, hour); // Set RTC time
        break;
    }
  }

  switch(mode)
  {
    case SET: // Time setting mode
      // Select digit
      if(buttonPressed[1])
      {
        selected_digit++;
        if(selected_digit > 3) selected_digit = 0;
      }   
      // Increase digit
      if(buttonPressed[2])
      {
        switch(selected_digit)
        {
        case 0:
          digit0++;
          if(digit0 > (digit1 > 3 ? 1 : 2)) digit0 = 0;
          break;
        case 1:
          digit1++;
          if(digit1 > (digit0 == 2 ? 3 : 9)) digit1 = 0;
          break;
        case 2:
          digit2++;
          if(digit2 > 5) digit2 = 0;
          break;
        case 3:
          digit3++;
          if(digit3 > 9) digit3 = 0;
          break;
        }
      }
      displayNum(digit0 * 1000 + digit1 * 100 + digit2 * 10 + digit3);
      break;
      
    case TIME: // Time dispay mode
    case IDLE:
      if(one_sec_elapsed)
      {
        // Retrieve the current time from the RTC
        rtc_get_time(&sec, &min, &hour);
        one_sec_elapsed = false;

        // Print the time on the serial monitor
        Serial.print(hour);
        Serial.print(":");
        Serial.print(min);
        Serial.print(":");
        Serial.println(sec);
      }
      displayNum(hour * 100 + min);
      break;
      
    default:
      mode = IDLE;
  }
}

ISR(TIMER1_COMPA_vect)
{
  one_sec_elapsed = true;
}

// Displays a numer on the 7-segment display
void displayNum(int num) 
{
  for(int i = 0; i < 4; i++)
  {
    // Setting the display pattern
    for(int j = 0; j < 7; j++)
    {
      int val = number[num % 10][j] << SEGMENT_PINS[j];
      if(j < 6) PORTD |= val;
      else PORTB |= val;
    }
    PORTB |= (1 << DIGIT_PINS[i]);
    
    // Blink the selected digit while keeping the other
    // digits on (only in SET mode)
    if(mode == SET && !(PINC & (1 << BUTTON_PINS[2])))
    {
      currBlinkMillis = millis();
      if(currBlinkMillis - prevBlinkMillis > 500)
      {
        prevBlinkMillis = currBlinkMillis;
        blink_state = !blink_state;
      }
      if(blink_state) 
        PORTB &= ~(1 << DIGIT_PINS[abs(selected_digit - 3)]);
    }
    delay(5);
    PORTB &= ~(1 << DIGIT_PINS[i]);
    PORTB &= ~(1 << SEGMENT_PINS[6]);
    PORTD &= B00000011;
    num /= 10;
  }
}

///////////////////////////////////RTC FUNCTIONS/////////////////////////////////////

// Converts a number from Decimal to BCD format to be sent to the RTC module
int DECtoBCD(int val) 
{
  return (val / 10 * 16) + (val % 10);
}

// Converts a number from BCD to Decimal format to be displayed
int BCDtoDEC(int val)
{
  return (val / 16 * 10) + (val % 16);
}

// RST pin must be ON during read/write
void ce_on()
{
  PORTC |= (1 << RTC_RST_PIN);
  delayMicroseconds(4); // CE to CLK Setup Time
}

// Turn off RST pin after read/write
void ce_off()
{
  PORTC &= ~(1 << RTC_RST_PIN);
  delayMicroseconds(4); // CE inactive Time
}

// Reads a single byte from the RTC module through the IO pin
int reg_read()
{
  int val = 0;
  
  // Set IO pin to Input mode
  DDRC &= ~(1 << RTC_IO_PIN);
    
  // Reading 8 bits
  for (int i = 0; i < 8; i++)
  {  
    // On falling edge, the next bit is sent from the RTC
    PORTC |= (1 << RTC_CLK_PIN);
    delayMicroseconds(1); // CLK High Time
    PORTC &= ~(1 << RTC_CLK_PIN);
    delayMicroseconds(1); // CLK Low Time
    
    if(PINC & (1 << RTC_IO_PIN))
      val |= (1 << i);
    else
      val &= ~(1 << i);
  }
  return val;
}

// Sends a single byte to the RTC module through the IO pin
void reg_write(const int val, bool readNext = false) 
{
  // Set IO pin to Ouput mode
  DDRC |= (1 << RTC_IO_PIN);
  
  // Writing 8 bits
  for (int i = 0; i < 8; i++) 
  {
    if(val & (1 << i))
      PORTC |= (1 << RTC_IO_PIN);
    else
      PORTC &= ~(1 << RTC_IO_PIN);      
    delayMicroseconds(1); // Data to CLK Setup Time(200ns)
    
    // On rising edge, the RTC reads the next bit
    PORTC |= (1 << RTC_CLK_PIN);
    delayMicroseconds(1);  // CLK High Time
    if (i == 7 && readNext) break;
    PORTC &= ~(1 << RTC_CLK_PIN);
    delayMicroseconds(1); // CLK Low Time
  }
}

// Sets the Write-Protect bit
void setWriteProtect(bool flag) 
{
  ce_on();
  reg_write(WP_REG);
  reg_write(flag << WP_BIT);
  ce_off();
}

// Sets the Halt flag
void setHalt(bool flag) {
  ce_on();
  reg_write(CH_REG);
  reg_write(flag << CH_BIT);
  ce_off();
}

// Pauses/Starts the RTC
void rtc_stop(bool stop)
{
  if(stop)
  {
    // Stop the RTC from incrementing the time
    // and enable the Write-Protect bit
    setWriteProtect(false);
    setHalt(true);
    setWriteProtect(true);
  }
  else
  {
    // Clear the Write-Protect bit and the CH flag
    // Must be done before attempting to write to the RTC
    setWriteProtect(false);
    setHalt(false);
  }
}

// Retrieves the time from the RTC module
void rtc_get_time(int *sec, int *min, int *hour) 
{
  ce_on();
  reg_write(CLK_BURST_READ_REG, true);
  *sec = BCDtoDEC(reg_read());
  *min = BCDtoDEC(reg_read());
  *hour = BCDtoDEC(reg_read());
  ce_off();
}

// Sets the time of the RTC module
void rtc_set_time(int sec, int min, int hour) 
{
  ce_on();
  PORTC |= (1 << RTC_RST_PIN);
  reg_write(CLK_BURST_WRITE_REG);
  reg_write(DECtoBCD(sec));
  reg_write(DECtoBCD(min));
  reg_write(DECtoBCD(hour));
  
  // We only need the time so setting
  // the remaining registers to 0
  for(int i = 0; i < 5; i++)
    reg_write(0);

  ce_off();
}
