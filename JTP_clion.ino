/* Code voor Join the Pipe Controllino Mini controllers
   All rights reserved Edwin van den Oetelaar (edwin@oetelaar.com)
   Gemaakt voor Visbeek elektro, installatie in concertgebouw
   De Inputs en outputs werken NIET goed als de 24 volt afwezig is
   == de leds knipperen wel, maar relais niet bekrachtigd.
   == de inputs werken dan niet goed, schakelaars reageren niet
   == let dus op, testen moet met 24V aanwezig en niet enkel power via USB bus
*/

#include <Arduino.h>
#include <EEPROM.h>

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0])) /* standard C linux way to get array size */

static const uint8_t PIN_KLEP1 = 4; /* klep 1, high is klep actuated */
static const uint8_t PIN_KLEP2 = 5; /* klep 2, high is klep actuated */
static const uint8_t PIN_LAMP1 = 6; /* signaal lamp, high is lamp actuated */
static const uint8_t PIN_BUZZER = 7; /* signaal buzzer,, high is actuated */

static const uint8_t PIN_SWITCH1 = 14; /* hoort bij klep 1 */
static const uint8_t PIN_SWITCH2 = 15; /* hoort bij klep 2 */

static const uint8_t my_outputs[] = {PIN_KLEP1, PIN_KLEP2, PIN_LAMP1, PIN_BUZZER};
static const uint8_t my_inputs[] = {PIN_SWITCH1, PIN_SWITCH2};

static const uint8_t my_config_start = 0x00; /* start offset  config in eeprom */
static const uint32_t my_version_constant = 0x1234;

static const int teachin_mode_ms = 10000;
static const int auto_mode_ms = 2000; /* tussen dit en teachin is auto mode */
static const int pause_mode_ms = 30000; /* tussen dit en teachin is auto mode */

typedef struct {
  uint32_t pulse_len_plat;
  uint32_t pulse_len_bubbles;
  uint32_t pause_len_plat;
  uint32_t pause_len_bubbles;
  /* config version check in eeprom */
  uint32_t version;
} settings_struct_t;

/* global settings variable in RAM */
typedef enum {
  OFF = 0x00, ON = 0x01, BLINK = 0x02, FASTBLINK = 0x03, SLOWBLINK = 0x04, DYNAMICBLINK = 0x05
} lamp_buzzer_state_t;

settings_struct_t ram_settings = {0,}; /* copy in RAM van EEPROM settings */

/* we maken een MOORE machine https://en.wikipedia.org/wiki/Moore_machine */
/* dit voorkomt spaghetti code en raar gedrag etc, dit is de simpelste manier om PLC's te maken */
/* het is een hybride machine, aangezien de uitgangen soms afhankelijk zijn van de inputs en niet enkel van de state */

typedef struct {
  int current_state;
  int next_state;
} moore_machine_t;

moore_machine_t SM = {0,}; /* global init machine */

typedef struct {
  uint8_t active;
  uint8_t expired;
  unsigned long timeout_millis;
} timer_t;

timer_t T1 = {0,}; /* global timer 1 */
timer_t T2 = {0,}; /* global timer 2 */
timer_t T3 = {0,}; /* global timer 3 */
timer_t T4 = {0,}; /* global timer 4 */

unsigned long cntr_fastblinker = 0; /* global blinkers */
unsigned long cntr_slowblinker = 0; /**/
unsigned long cntr_slowerblinker = 0; /**/
unsigned long cntr_dynamicblinker = 0; /* voor pause teach lamp interval */
unsigned long measurement_start_millies = 0;
unsigned long measurement_stop_millies = 0;
uint32_t lamp_interval = 0;

bool flag_slow_blinker = false;
bool flag_slower_blinker = false;
bool flag_fast_blinker = false; // led or buzzer can use these to blink
bool flag_dynamic_blinker = false; //

int timer_activate(timer_t *t, unsigned long ts) {
  t->timeout_millis = ts;
  t->active = 1;
  t->expired = 0;
  return 0;
}

int timer_update(timer_t *t, unsigned long ts) {
  if (t->active) {
    if (ts >= t->timeout_millis) {
      /* expired */
      t->expired = 1;
      t->active = 0;
    }
  } else {
    /* nothing to do */

  }
  return 0;
}

uint8_t timer_is_expired(const timer_t *t) {
  return (uint8_t) (t->expired && (t->active == 0));
}

uint8_t timer_stop(timer_t *t) {
  t->active = 0;
  t->expired = 0;
  t->timeout_millis = 0;
  return 0;
}

int load_eeprom_config(settings_struct_t *s) {
  /* load data into struct s */
  s->version = my_version_constant;
  s->pulse_len_bubbles = 2000;
  s->pulse_len_plat = 5000;
  s->pause_len_bubbles = 500;
  s->pause_len_plat = 500;
  //return 0; /* OK */
  // To make sure there are settings, and they are YOURS!
  for (uint16_t i = 0; i < sizeof(settings_struct_t); i++) {
    uint8_t b = EEPROM.read(my_config_start + i);
    Serial.print(i);
    Serial.print(" byte r = ");
    Serial.println(b, HEX);
    // *((uint8_t *) (s + i)) = b;
  }

  eeprom_read_block(s, 0, sizeof(settings_struct_t));
  /* tijdelijk forceren 500 ms */
  if ((s->pause_len_plat < 300) || ( s->pause_len_plat > 5000)) {
    s->pause_len_plat = 1500;
  }
  if ((s->pause_len_bubbles < 300) || ( s->pause_len_bubbles > 5000)) {
    s->pause_len_bubbles = 1500;
  }
  return 0; /* ERROR*/
}

int save_eeprom_config(const settings_struct_t *s) {
  for (uint16_t i = 0; i < sizeof(settings_struct_t); i++) {
    // EEPROM.write(CONFIG_START + i, *((char*)&storage + i));
    Serial.print("write ");
    Serial.print(i);
    Serial.print(" => ");
    Serial.print(*((uint8_t *) (s + i)), HEX);
    Serial.println("");
    // EEPROM.update(my_config_start + i, *((uint8_t *) (s + i)));
    eeprom_write_block(s, 0, sizeof(settings_struct_t));
  }

  return 0; /* OK */
}

// the setup function runs once when you press reset or power the board
void setup() {
  Serial.begin(57600); // initialize Serial communication
  while (!Serial);    // wait for the serial port to open

  // initialize device
  Serial.println("JTP init");
  /* set up init output pins */
  uint8_t i = 0;
  for (i = 0; i < ARRAY_SIZE(my_outputs); i++) {
    pinMode(my_outputs[i], OUTPUT);
  }

  /* set up all inputs */
  for (i = 0; i < ARRAY_SIZE(my_inputs); i++) {
    pinMode(my_inputs[i], INPUT);
  }

  Serial.println("JTP init 2");
  int rv = load_eeprom_config(&ram_settings);
  Serial.print("rv=");
  Serial.println(rv);
  Serial.print("ver=");
  Serial.println(ram_settings.version);

}

/*
   state          input1 input2 output1 output2 output3
   0              0      0      0       0       1
   0              0      1      0       1       1
   0              1      0      1       0       1
   0              1      1      0       0       1 => trigger mode change next state=1, start timer1
   1              1      1      0       0       1 => blijven hangen
 * */

void print_timer(timer_t *t, const char *s) {
  Serial.print("Timer :");
  Serial.print(s);
  Serial.print(" exp=");
  Serial.print(t->expired);
  Serial.print(" to=");
  Serial.print(t->timeout_millis);
  Serial.print(" act=");
  Serial.println(t->active);
}


void loop() {
  /* read all inputs just once */

  int sw1_in = digitalRead(PIN_SWITCH1);
  int sw2_in = digitalRead(PIN_SWITCH2);

  /* define all default values for outputs */

  uint8_t k1_out = 0x00;
  uint8_t k2_out = 0x00;

  /* handle all timers, they generate virtual inputs (as they expire) */
  unsigned long time_now = millis();
  unsigned long pulse_lengte = 0;

  /* alle timers checken */
  timer_update(&T1, time_now);
  timer_update(&T2, time_now);
  timer_update(&T3, time_now);
  timer_update(&T4, time_now);

  /* update blinkers */
  if ((cntr_slowblinker - time_now) > 250) {
    cntr_slowblinker = time_now + 250;
    flag_slow_blinker = !flag_slow_blinker;
  }
  if ((cntr_fastblinker - time_now) > 150) {
    cntr_fastblinker = time_now + 150;
    flag_fast_blinker = !flag_fast_blinker;
  }
  if ((cntr_slowerblinker - time_now) > 500) {
    cntr_slowerblinker = time_now + 500;
    flag_slower_blinker = !flag_slower_blinker;
  }

  if ((cntr_dynamicblinker - time_now) > lamp_interval) {
    cntr_dynamicblinker = time_now + lamp_interval;
    flag_dynamic_blinker = !flag_dynamic_blinker;
  }


  /* virtuele inputs, uitgang van de timers, als ze verlopen zijn */
  uint8_t timer_1_expired = timer_is_expired(&T1); /* timer voor 4 seconden 2 key press teach in mode */
  uint8_t timer_2_expired = timer_is_expired(&T2); /* timer voor pulse lengte water klep 1 en 2 */
  uint8_t timer_3_expired = timer_is_expired(&T3); /* timer voor expire van teach in mode, als geen actie komt */
  uint8_t timer_4_expired = timer_is_expired(&T4); /* timer voor beep na teach in */
  /* virtuele output, bijv de lamp, die kan knipperen */

  lamp_buzzer_state_t lamp_out = OFF;
  lamp_buzzer_state_t buzzer_out = OFF;

  /* debug naar serial */

  //  print_timer(&T1, "T1");
  // print_timer(&T3, "T3");

  /* the state logic */

  switch (SM.current_state) {
    case 0:
      /* maar als het mode switch is ... uitgangen niet bekrachtigen */
      if (sw1_in && sw2_in) {
        //k1_out = 0x00;
        //k2_out = 0x00;
        lamp_out = ON; /* ON */
        //buzzer_out = 0x00;
        SM.next_state = 1; /* wacht op timer van 4 seconden */
        timer_activate(&T1, time_now + pause_mode_ms);
        //                T1.timeout_millis = time_now + 4000; /* expire over 4 seconden */
      } else if (sw1_in == 0 && sw2_in == 0) {
        /* als er geen knoppen zijn ingedrukt, dan blijven we hier */
        SM.next_state = 0;
        //    k1_out = 0x00; /* */
        //    k2_out = 0x00;
        lamp_out = ON; /* ON */
        //    buzzer_out =0x00;
      } else if (sw1_in) {
        /* als er 1 knop (S1) is ingedrukt dan gaan we timer met plat of bubbles doen */
        k1_out = 0x01; // plat water
        lamp_out = ON; /* ON */
        SM.next_state = 3;
        timer_activate(&T2, time_now + ram_settings.pulse_len_plat);
      } else if (sw2_in) {
        /* als er 1 knop (S2) is ingedrukt dan gaan we timer met plat of bubbles doen */
        k2_out = 0x01; // bubbles
        lamp_out = ON; /* ON */
        SM.next_state = 4;
        timer_activate(&T2, time_now + ram_settings.pulse_len_bubbles);
      } else {
        Serial.print("Can not happen 1");
      }

      break;
    case 1:
      /* 30 seconden wachten ivm detect van mode change */
      /* beide ingangen moeten hoog blijven en de timer moet aflopen, dan gaan we verder.. anders terug naar 0 */
      if (sw1_in && sw2_in) {
        k1_out = 0x00;
        k2_out = 0x00;
        lamp_out = ON; /* ON */
        if (timer_1_expired) {
          SM.next_state = 12;
        } else {
          SM.next_state = SM.current_state; /* wacht op timer */
        }

      } else {
        /* knoppen niet meer beiden ingedrukt */
        SM.next_state = 20;
      }

      break;

    case 12:
      SM.next_state = 13;
      Serial.println("pauze modus");
      timer_activate(&T3, time_now + 30000);

      break;

    case 13:
      lamp_out = DYNAMICBLINK; /* timer gebruikt nu lamp_interval voor timing van pulsen */
      lamp_interval = ram_settings.pause_len_plat;
      SM.next_state = SM.current_state;
      if (timer_3_expired) {
        Serial.println("timer3 expired, pause teach in done");
        save_eeprom_config(&ram_settings);
        SM.next_state = 0;
      }

      if (sw2_in) {
        lamp_interval += 100;
        /* nu wachten op release van sw1 */
        SM.next_state = 15;

      }

      if (sw1_in) {
        lamp_interval -= 100;
        /* nu wachten op release van sw1 */
        SM.next_state = 14;

      }

      if (lamp_interval < 300) {
        lamp_interval = 300;
      }

      if (lamp_interval > 5000) {
        lamp_interval = 5000;
      }

      Serial.println(lamp_interval);
      break;

    case 14:
      lamp_out = DYNAMICBLINK; /* timer gebruikt nu lamp_interval voor timing van pulsen */
      ram_settings.pause_len_plat = lamp_interval;
      ram_settings.pause_len_bubbles = lamp_interval;
      SM.next_state = SM.current_state;
      if (sw1_in == 0) {
        SM.next_state = 13;
        timer_activate(&T3, time_now + 30000);
      }

      break;

    case 15:
      lamp_out = DYNAMICBLINK; /* timer gebruikt nu lamp_interval voor timing van pulsen */
      ram_settings.pause_len_plat = lamp_interval;
      SM.next_state = SM.current_state;
      if (sw2_in == 0) {
        SM.next_state = 13;
        timer_activate(&T3, time_now + 30000);
      }

      break;


    case 20:
      /* wacht tot beide knoppen los zijn */
      k1_out = 0x00;
      k2_out = 0x00;
      lamp_out = ON;
      if (sw1_in || sw2_in) {
        SM.next_state = SM.current_state;
        Serial.println("x");
      } else {
        /* geen keys meer, dan naar state 0 */
        SM.next_state = 0;
        unsigned long niet_verstreken = T1.timeout_millis - time_now;
        Serial.print("time remain ");
        Serial.println(niet_verstreken);

        unsigned long t_1 = (pause_mode_ms - auto_mode_ms); /* 30000 - 2000 */
        unsigned long t_2 = (pause_mode_ms - teachin_mode_ms); /* 30000 - 10000 */

        if (niet_verstreken > t_2 && niet_verstreken < t_1  ) {
          /* */
          SM.next_state = 21;
          timer_activate(&T3, time_now + 30000);
          timer_stop(&T1);
        } else if (niet_verstreken < t_2) {
          /* tussen de 10 en 30 sec naar state=2 */
          SM.next_state = 2;
          timer_stop(&T1);
        } else {
          SM.next_state = 0;
          timer_stop(&T1);
        }
      }
      break;

    case 21:
      /* wacht op 1 toets, deze wordt automatisch */
      /* timeout toevoegen */
      lamp_out = SLOWBLINK;
      SM.next_state = SM.current_state;
      if (timer_is_expired(&T3)) {
        Serial.println("timeout in auto mode");
        SM.next_state = 0;
      }
      if (sw1_in) {
        /* plat water klep 1 automaat */
        SM.next_state = 66;
      }

      if (sw2_in) {
        SM.next_state = 67;
      }

      break;

    case 66:
      /* wacht op release van sw1 alvorens naar 22 te gaan */
      if (sw1_in) {
        /* hangen hier */
        SM.next_state = SM.current_state;
      } else {
        /* door naar 22 */
        SM.next_state = 22;
      }
      break;


    case 67:
      /* wacht op release van sw2 alvorens naar 23 te gaan */
      if (sw2_in) {
        /* hangen hier */
        SM.next_state = SM.current_state;
      } else {
        /* door naar 23 */
        SM.next_state = 23;
      }
      break;

    case 22:
      Serial.println("klep 1 auto");
      SM.next_state = 24;
      timer_activate(&T2, time_now + ram_settings.pause_len_plat);
      break;

    case 24:
      /* wachten klep 1 auto */
      lamp_out = FASTBLINK;
      if (timer_is_expired(&T2)) {
        SM.next_state = 25;
        timer_activate(&T2, time_now + ram_settings.pulse_len_plat);
        Serial.println("T2 pauze done");
      }
      /* wacht op timer of op toets */
      if (sw1_in || sw2_in) {
        SM.next_state = 99;
        timer_stop(&T2);
      }

      break;

    case 25:
      /* water geven klep 1 auto */
      k1_out = 0x01;
      lamp_out = FASTBLINK;
      if (timer_is_expired(&T2)) {
        SM.next_state = 24;
        timer_activate(&T2, time_now + ram_settings.pause_len_plat);
        Serial.println("T2 pulse done");
      }

      /* wacht op timer of toets */
      if (sw1_in || sw2_in) {
        SM.next_state = 99;
        timer_stop(&T2);
      }
      break;


    case 23:
      Serial.println("klep 2 auto");
      // SM.next_state = SM.current_state;
      SM.next_state = 54;
      timer_activate(&T2, time_now + ram_settings.pause_len_bubbles);
      break;
    /* == */
    case 54:
      /* wachten klep 1 auto */
      lamp_out = FASTBLINK;
      if (timer_is_expired(&T2)) {
        SM.next_state = 55;
        timer_activate(&T2, time_now + ram_settings.pulse_len_bubbles);
        Serial.println("T2 pauze done");
      }
      /* wacht op timer of op toets */
      if (sw1_in || sw2_in) {
        SM.next_state = 99;
        timer_stop(&T2);
      }

      break;

    case 55:
      /* water geven klep 1 auto */
      k2_out = 0x01;
      lamp_out = FASTBLINK;
      if (timer_is_expired(&T2)) {
        SM.next_state = 54;
        timer_activate(&T2, time_now + ram_settings.pause_len_bubbles);
        Serial.println("T2 pulse done");
      }

      /* wacht op timer of toets */
      if (sw1_in || sw2_in) {
        SM.next_state = 99;
        timer_stop(&T2);
      }
      break;


    /* == */


    case 2:
      SM.next_state = 2; /* hangen hierzo */
      /* van hieruit gaan we na 30 seconden terug naar state 0 omdat er niks gebeurde
         of we gaan de pulselengte meten van s1 of s2 */
      /* eerste wachten tot beide knoppen weer los zijn */
      lamp_out = BLINK; /*  */
      if (sw1_in || sw2_in) {
        /* nog steeds knop */
      } else {
        /* knop is los */
        SM.next_state = 80; // nu pulse meting
      }

      break;

    case 3:
      /* knop 1 nog ingedrukt ?? */
      lamp_out = FASTBLINK;
      k1_out = 0x01;

      if (sw1_in) {
        SM.next_state = SM.current_state;
        /* detect mode change */
        if (sw1_in && sw2_in) {
          SM.next_state = 1; /* wacht op timeout */
          timer_activate(&T1, time_now + pause_mode_ms);
        }
      } else {
        /* knop los, wacht nu op timer expire in andere state */
        SM.next_state = 5;
      }
      break;

    case 4:
      /* knop 2 nog ingedrukt ?? */
      lamp_out = FASTBLINK;
      k2_out = 0x01;

      if (sw2_in) {
        SM.next_state = SM.current_state;
        /* detect mode change */
        if (sw1_in && sw2_in) {
          SM.next_state = 1; /* wacht op timeout */
          timer_activate(&T1, time_now + pause_mode_ms);
        }
      } else {
        /* knop los, wacht nu op timer expire in andere state */
        SM.next_state = 6;
      }
      break;

    case 5:
      /* timeout van PLAT water checken, of knop herstart => stop alles */
      if (timer_2_expired) {
        lamp_out = ON;
        SM.next_state = 0;
      } else {
        if (sw1_in) {
          /* knop gedrukt tijdens timer, dat betekent stop nu */
          SM.next_state = 99;
          lamp_out = ON;
        } else {
          SM.next_state = SM.current_state; /* blijven hangen totdat timer afloopt of knop komt */
          k1_out = 0x01;
          lamp_out = FASTBLINK;
        }
      }
      break;

    case 6:
      /* timeout van BUBBLES water checken, of knop herstart => stop alles */
      if (timer_2_expired) {
        lamp_out = ON;
        SM.next_state = 0;
      } else {
        if (sw2_in) {
          /* knop gedrukt tijdens timer, dat betekent stop nu */
          SM.next_state = 99;
          lamp_out = ON;
        } else {
          SM.next_state = SM.current_state; /* blijven hangen totdat timer afloopt of knop komt */
          k2_out = 0x01;
          lamp_out = FASTBLINK;
        }
      }
      break;

    case 80:
      /* we komen hier in teach in mode, beide inputs zijn NIET ingedrukt nu */
      /* de eerste toets die we zien gaan we inleren, zien we 30 seconden niks dan zijn we klaar en is er niks geleerd */
      /* hier zetten we een timeout */
      timer_activate(&T3, time_now + 30000);
      SM.next_state = 81;
      break;

    case 81: /* wacht op timeout */
      lamp_out = BLINK; /*  */
      SM.next_state = SM.current_state;
      if (timer_3_expired) {
        Serial.println("timer3 expired, no teach in done");
        SM.next_state = 0;
      }

      if (sw1_in) {
        /* key1 gedrukt, we gaan inleren */
        SM.next_state = 82;
        timer_stop(&T3);
      }

      if (sw2_in) {
        /* key2 gedrukt, we gaan inleren */
        SM.next_state = 92;
        timer_stop(&T3);
      }

      break;

    /* pulse meten voor SW1 */
    case 82:
      /* inleren van key 1 */
      if (sw1_in) {
        measurement_start_millies = time_now;
        k1_out = 0x01;
        SM.next_state = 83;
      } else {
        /* key was niet ingedrukt, vreemd doe niks */
        SM.next_state = 0;
      }
      break;

    case 83:
      /* wacht op loslaten van key 1 */
      if (!sw1_in) {
        measurement_stop_millies = time_now;
        k1_out = 0x00;
        SM.next_state = 84;
      } else {
        /* key was nog ingedrukt, we hangen hier te wachten */
        /* uitgang moet water geven */
        k1_out = 0x01;
        SM.next_state = SM.current_state;
      }

      break;

    case 84:
      Serial.print("de lengte van de puls was klep 1:");
      pulse_lengte = measurement_stop_millies - measurement_start_millies;
      Serial.println(pulse_lengte);
      // nu opslaan indien redelijke waarde is...
      if (pulse_lengte > 500) {
        Serial.println("opslaan pulselengte > 500 : ");
        ram_settings.pulse_len_plat = (uint32_t) pulse_lengte;
        save_eeprom_config(&ram_settings);
      } else {
        Serial.println("Niet opslaan pulselengte < 500 : ");
      }
      SM.next_state = 0;
      break;

    /* pulse meten voor SW2 */
    case 92:
      /* inleren van key 1 */
      if (sw2_in) {
        measurement_start_millies = time_now;
        k2_out = 0x01;
        SM.next_state = 93;
      } else {
        /* key was niet ingedrukt, vreemd doe niks */
        SM.next_state = 0;
      }
      break;

    case 93:
      /* wacht op loslaten van key 1 */
      if (!sw2_in) {
        measurement_stop_millies = time_now;
        k2_out = 0x00;
        SM.next_state = 94;
      } else {
        /* key was nog ingedrukt, we hangen hier te wachten */
        /* uitgang moet water geven */
        k2_out = 0x01;
        SM.next_state = SM.current_state;
      }

      break;

    case 94:
      Serial.print("de lengte van de puls was klep 2:");
      pulse_lengte = measurement_stop_millies - measurement_start_millies;
      Serial.println(pulse_lengte);
      // nu opslaan indien redelijke waarde is... > 500 ms
      if (pulse_lengte > 500) {
        Serial.println("opslaan pulselengte klep 2 > 500 : ");
        ram_settings.pulse_len_bubbles = (uint32_t) pulse_lengte;
        save_eeprom_config(&ram_settings);
      } else {
        Serial.println("Niet opslaan pulselengte < 500 : ");
      }
      SM.next_state = 0;
      break;

    /* wacht op alle release, na probleem */
    case 99:
      /* er is wat gebeurd, we wachten tot alle knoppen zijn losgelaten en sturen geen water */
      if (sw1_in || sw2_in) {
        /* er zijn nog knoppen, blijf wachten */
        lamp_out = OFF;
        SM.next_state = SM.current_state;
      } else {
        SM.next_state = 0;
      }
      break;

    default:
      Serial.println("Undef state");

  }

  /* output normal valves */
  digitalWrite(PIN_KLEP1, k1_out);
  digitalWrite(PIN_KLEP2, k2_out);

  /* complexe virtuele outputs */
  if (lamp_out == OFF || lamp_out == ON) {
    /* output LED */
    digitalWrite(PIN_LAMP1, lamp_out);
  } else if (lamp_out == FASTBLINK) {
    digitalWrite(PIN_LAMP1, (uint8_t) flag_fast_blinker);
  } else if (lamp_out == BLINK) {
    digitalWrite(PIN_LAMP1, (uint8_t) flag_slow_blinker);
  } else if (lamp_out == SLOWBLINK) {
    digitalWrite(PIN_LAMP1, (uint8_t) flag_slower_blinker);
  } else if (lamp_out == DYNAMICBLINK) {
    digitalWrite(PIN_LAMP1, (uint8_t) flag_dynamic_blinker);
  }
  /* timer stuff */


  /* complexe virtuele outputs */
  if (buzzer_out == OFF || buzzer_out == ON) {
    /* output LED */
    digitalWrite(PIN_BUZZER, buzzer_out);
  } else if (buzzer_out == FASTBLINK) {
    /* timer stuff */
    digitalWrite(PIN_BUZZER, (uint8_t) flag_fast_blinker);
  } else if (buzzer_out == BLINK) {
    digitalWrite(PIN_BUZZER, (uint8_t) flag_slow_blinker);
  }


  /* output Buzzer */
  // digitalWrite(PIN_BUZZER, buzzer_out);
  if (SM.current_state != SM.next_state) {
    Serial.print(SM.current_state);
    Serial.print(" ");

    Serial.println(SM.next_state);
    Serial.print(" ");
  }
  //  Serial.println(flag_fast_blinker);
  //  Serial.print(" ");

  //  Serial.println(flag_slow_blinker);


  /* update state machine transition */
  SM.current_state = SM.next_state;
}
