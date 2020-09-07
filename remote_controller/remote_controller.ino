#include <LiquidCrystal_I2C.h>
#include <RF24.h>
#include <Wire.h>
#include <nRF24L01.h>
#include <printf.h>

#define VERBOSE 1

/* Conversion values from 0/1023 to 0/255,
   my potentiometers bounds when are centered to 530
*/
#define pot_min_right_x 160
#define pot_max_right_x 915
#define pot_min_right_y 190
#define pot_max_right_y 865
#define pot_min_left_x 140
#define pot_max_left_x 935
#define pot_min_left_y 195
#define pot_max_left_y 880

// Menù button pin
#define button1_pin 2  // lower button, orange wire
#define button2_pin 4  // middle button, green wire
#define button3_pin 3  // upper button, red wire

// Potentiometers, switch and nRF24L01 pin
#define pot_right_x_pin 1
#define pot_right_y_pin 0
#define pot_left_x_pin 2
#define pot_left_y_pin 3
#define switch_left_pin 7
#define switch_right_pin 8
#define ce_pin 5
#define csn_pin 6

// LCD pin
#define lcd_row 2
#define lcd_col 16
#define lcd_backlight_timeout 5000

// Number of different node for nrf24l01
#define node_name_number 3
#define node_name_length 5

bool buttonState1 = false;
bool buttonState2 = false;
bool buttonState3 = false;
int pot_right_x_state = 0;
int pot_right_y_state = 0;
int pot_left_x_state = 0;
int pot_left_y_state = 0;
byte switch_left_state = 0;
byte switch_right_state = 0;

unsigned long last_call_int = 0;
unsigned long last_call_int3 = 0;

String lcd_line0;
String lcd_line1;
String empty_line = "                ";
unsigned long last_lcd_backlight = 0;
unsigned long last_online_rec = 0;
bool rec_online = true;
byte main_menu = 0;
bool show_menu = false;

// VAriables for NRF24L01
RF24 radio(ce_pin, csn_pin);  // nRF24L01 (CE, CSN)
const byte nodeAddresses[node_name_number][node_name_length] = {"node1", "node2", "node3"};
int selected_node = 1;

// Variables for LCD
// set the LCD address to 0x27 for a 16 chars and 2 line display
LiquidCrystal_I2C lcd(0x27, lcd_col, lcd_row);

// Data package for comunication through nrf24l01
struct Data_Package {
    byte pot_right_x;
    byte pot_right_y;
    byte pot_left_x;
    byte pot_left_y;
    byte switch_left;
    byte switch_right;
};

Data_Package data;  // Create a variable with the above structure

// Main Menu stuff
String mainMenuItems[] = {
    "Change Rec", "Set backlight", "ITEM 3", "ITEM 4", "ITEM 5", "ITEM 6",
};
int mainMenuItemsSize = sizeof(mainMenuItems) / sizeof(String);
int mainMenuPage = 0;
int maxMainMenuPages = round(((sizeof(mainMenuItems) / sizeof(String)) / 2) + .5);
int mainCursorPosition = 0;

// Sub Menu 1 Stuff
String subMenu1Items[node_name_number] = {"node1", "node2", "node3"};
int subMenu1ItemsSize = sizeof(subMenu1Items) / sizeof(String);
int subMenu1Page = 0;
int maxSubMenu1Pages = round(((sizeof(subMenu1Items) / sizeof(String)) / 2) + .5);
int subMenu1CursorPosition = 0;

// Creates 3 custom characters for the menu display
byte downArrow[8] = {
    0b00100,  //   *
    0b00100,  //   *
    0b00100,  //   *
    0b00100,  //   *
    0b00100,  //   *
    0b10101,  // * * *
    0b01110,  //  ***
    0b00100   //   *
};

byte upArrow[8] = {
    0b00100,  //   *
    0b01110,  //  ***
    0b10101,  // * * *
    0b00100,  //   *
    0b00100,  //   *
    0b00100,  //   *
    0b00100,  //   *
    0b00100   //   *
};

byte menuCursor[8] = {
    B01000,  //  *
    B00100,  //   *
    B00010,  //    *
    B00001,  //     *
    B00010,  //    *
    B00100,  //   *
    B01000,  //  *
    B00000   //
};

void setup() {
// start serial connection
#if VERBOSE
    Serial.begin(9600);
#endif

    // Menù input pin button configuration
    pinMode(button1_pin, INPUT_PULLUP);
    pinMode(button2_pin, INPUT_PULLUP);
    pinMode(button3_pin, INPUT_PULLUP);

    // Switch configuration
    pinMode(switch_left_pin, INPUT_PULLUP);
    pinMode(switch_right_pin, INPUT_PULLUP);

    // Configuration interrupt menù button (button 3 and 2 are connected to the same gpio, 
    // but the second is also connected to another gpio with diode)
    attachInterrupt(digitalPinToInterrupt(button3_pin), button32_callback, FALLING);
    attachInterrupt(digitalPinToInterrupt(button1_pin), button1_callback, FALLING);

    subMenu1Items[selected_node] += " (sel)";

    // Radio configuration
    radio.begin();
    // radio.openWritingPipe(address);
    radio.setAutoAck(true);
    radio.setChannel(0x76);
    radio.setRetries(1, 15);
    radio.enableAckPayload();
    radio.setDataRate(RF24_250KBPS);
    radio.setPALevel(RF24_PA_LOW);

    // LCD configuration
    lcd.init();
    lcd.backlight();
    // Creates the byte for the 3 custom characters
    lcd.createChar(0, menuCursor);
    lcd.createChar(1, upArrow);
    lcd.createChar(2, downArrow);

    lcd_line0 = "RC trasmitter";
    lcd_line1 = "Dario Gerosa";

    updateDisplay();
    delay(2000);
}

void loop() {
    pot_right_x_state = analogRead(pot_right_x_pin);
    pot_right_y_state = analogRead(pot_right_y_pin);
    pot_left_x_state = analogRead(pot_left_x_pin);
    pot_left_y_state = analogRead(pot_left_y_pin);
    switch_left_state = digitalRead(switch_left_pin);
    switch_right_state = digitalRead(switch_right_pin);
    
    // Debugging stuff
    //  lcd_line0 = String(switch_left_state, DEC) + ", "
    //              + String(switch_right_state, DEC) + ", "
    //              + String(buttonState1, DEC) + ", "
    //              + String(buttonState2, DEC) + ", "
    //              + String(buttonState3, DEC);
    //
    //  lcd_line1 = String(pot_right_x_state, DEC)
    //              + "," + String(pot_right_y_state, DEC)
    //              + "," + String(pot_left_x_state, DEC)
    //              + "," + String(pot_left_y_state, DEC);

#if VERBOSE
    Serial.print(String(pot_right_x_state, DEC));
    Serial.print(", ");
    Serial.print(String(pot_right_y_state, DEC));
    Serial.print(", ");
    Serial.print(String(pot_left_x_state, DEC));
    Serial.print(", ");
    Serial.println(String(pot_left_y_state, DEC));
#endif

    send_data();
    updateMenu();
    updateDisplay();

    delay(50);
}

void send_data() {
    data.pot_right_x = map(pot_right_x_state, pot_min_right_x, pot_max_right_x, 0, 255);
    data.pot_right_y = map(pot_right_y_state, pot_min_right_y, pot_max_right_y, 0, 255);
    data.pot_left_x = map(pot_left_x_state, pot_min_left_x, pot_max_left_x, 0, 255);
    data.pot_left_y = map(pot_left_y_state, pot_min_left_y, pot_max_left_y, 0, 255);
    data.switch_left = switch_left_state;
    data.switch_right = switch_right_state;

    radio.openWritingPipe(nodeAddresses[selected_node]);

    if (radio.write(&data, sizeof(Data_Package))) {
        if (!rec_online) {
            lcd_line0 = "Receiver " + getNodeName(nodeAddresses[selected_node]);
            lcd_line1 = "online!";
            last_lcd_backlight = last_online_rec = millis();
        } else {
            if (millis() - last_online_rec > 5000) {
                printDataPackage();
            }
        }

        rec_online = true;
#if VERBOSE
        Serial.println("inviato");
#endif
    } else {
        if (rec_online) {
            lcd_line0 = "No ack from";
            lcd_line1 = "receiver " + getNodeName(nodeAddresses[selected_node]);
            last_lcd_backlight = last_online_rec = millis();
        } else {
            if (millis() - last_online_rec > 5000) {
                printDataPackage();
            }
        }
        rec_online = false;
#if VERBOSE
        Serial.println("no invio");
#endif
    }
}

void printDataPackage() {
    lcd_line0 = String(data.switch_left, DEC) + ", " + String(data.switch_right, DEC) + ", " +
                String(buttonState1, DEC) + ", " + String(buttonState2, DEC) + ", " + String(buttonState3, DEC);

    lcd_line1 = String(data.pot_right_x, DEC) + "," + String(data.pot_right_y, DEC) + "," +
                String(data.pot_left_x, DEC) + "," + String(data.pot_left_y, DEC);
}

void updateDisplay() {
    while (lcd_line1.length() <= lcd_col) {
        lcd_line1 += " ";
    }
    while (lcd_line0.length() <= lcd_col) {
        lcd_line0 += " ";
    }

    lcd.setCursor(0, 0);
    lcd.print(lcd_line0);
    lcd.setCursor(0, 1);
    lcd.print(lcd_line1);

    if (millis() - last_lcd_backlight > lcd_backlight_timeout) {
        lcd.noBacklight();
    } else {
        lcd.backlight();
    }
}

void updateMenu() {
    if (buttonState2) {
        operateMainMenu();
    }
}

void menuDraw(String menuItems[], int menuItemsSize, int menuPage, int maxMenuPages) {
    lcd_line0 = " " + menuItems[menuPage];
    lcd_line1 = " " + ((menuPage + 1) <= (menuItemsSize - 1) ? menuItems[menuPage + 1] : " ");
    
    while (lcd_line1.length() <= lcd_col) {
        lcd_line1 += " ";
    }
    while (lcd_line0.length() <= lcd_col) {
        lcd_line0 += " ";
    }
    
    if (menuPage == 0) {
        lcd_line1.setCharAt(15, char(2));
    } else if (menuPage > 0 and menuPage < maxMenuPages) {
        lcd_line1.setCharAt(15, char(2));
        lcd_line0.setCharAt(15, char(1));
    } else if (menuPage == maxMenuPages) {
        lcd_line0.setCharAt(15, char(1));
    }
}

// When called, this function will erase the current cursor and redraw it based on the cursorPosition and menuPage
// variables.
void drawCursor(int menuPage, int cursorPosition) {
    lcd_line0.setCharAt(0, ' ');
    lcd_line1.setCharAt(0, ' ');
    // The menu is set up to be progressive (menuPage 0 = Item 1 & Item 2, menuPage 1 = Item 2 & Item 3, menuPage 2
    // = Item 3 & Item 4), so in order to determine where the cursor should be you need to see if you are at an odd
    // or even menu page and an odd or even cursor position.
    if (menuPage % 2 == 0) {
        if (cursorPosition % 2 == 0) {  // If the menu page is even and the cursor position is even that means the
                                        // cursor should be on line 1
            lcd_line0.setCharAt(0, char(0));
        }
        if (cursorPosition % 2 != 0) {  // If the menu page is even and the cursor position is odd that means the
                                        // cursor should be on line 2
            lcd_line1.setCharAt(0, char(0));
        }
    }
    if (menuPage % 2 != 0) {
        if (cursorPosition % 2 == 0) {  // If the menu page is odd and the cursor position is even that means the
                                        // cursor should be on line 2
            lcd_line1.setCharAt(0, char(0));
        }
        if (cursorPosition % 2 != 0) {  // If the menu page is odd and the cursor position is odd that means the
                                        // cursor should be on line 1
            lcd_line0.setCharAt(0, char(0));
        }
    }
}

void operateMainMenu() {
    int activeButton = 0;
    buttonState1 = false;
    buttonState2 = false;
    buttonState3 = false;
    mainCursorPosition = 0;

    while (activeButton == 0) {
        if (buttonState1 && buttonState3) {
            Serial.println("indietro");
            activeButton = 1;

        } else if (buttonState2) {
            // This case will execute if the "forward" button is pressed
            switch (mainCursorPosition) {  // The case that is selected here is dependent on which menu page you are on and
                                       // where the cursor is.
                case 0:
                    menuItem1();
                    break;
                    /*case 1:
                        menuItem2();
                        break;
                    case 2:
                        menuItem3();
                        break;
                    case 3:
                        menuItem4();
                        break;
                    case 4:
                        menuItem5();
                        break;
                    case 5:
                        menuItem6();
                        break;
                    case 6:
                        menuItem7();
                        break;
                    case 7:
                        menuItem8();
                        break;
                    case 8:
                        menuItem9();
                        break;
                    case 9:
                        menuItem10();
                        break;*/
            }
            Serial.print("buttonstate2: ");
            Serial.println(buttonState2);

        } else if (buttonState3) {
            menuUp(mainMenuPage, mainCursorPosition, maxMainMenuPages, mainMenuItemsSize);
            Serial.print("buttonstate1: ");
            Serial.println(buttonState1);

        } else if (buttonState1) {
            menuDown(mainMenuPage, mainCursorPosition, maxMainMenuPages, mainMenuItemsSize);
            Serial.print("buttonstate1: ");
            Serial.println(buttonState1);

        }
        menuDraw(mainMenuItems, mainMenuItemsSize, mainMenuPage, maxMainMenuPages);
        drawCursor(mainMenuPage, mainCursorPosition);
        buttonState1 = false;
        buttonState2 = false;
        buttonState3 = false;
        Serial.println("operateMainMenu");
        Serial.print("mainMenuPage: ");
        Serial.println(mainMenuPage);
        updateDisplay();
    }
}

void menuItem1() {  // Function executes when you select the 2nd item from main menu
    int activeButton = 0;
    lcd_line0 = " ";
    lcd_line1 = " ";
    buttonState1 = false;
    buttonState2 = false;
    buttonState3 = false;
    subMenu1CursorPosition = 0;

    while (activeButton == 0) {
        if (buttonState1 && buttonState3) {
            Serial.println("indietro");
            activeButton = 1;
        } else if(buttonState2){
            subMenu1Items[selected_node].replace(" (sel)", "");
            selected_node = subMenu1CursorPosition;

            lcd_line0 = subMenu1Items[subMenu1CursorPosition];
            lcd_line1 = "selected";
            subMenu1Items[subMenu1CursorPosition] += " (sel)";

            updateDisplay();
            delay(2000);
            activeButton = 1;
            break;

        } else if (buttonState3) {
            menuUp(subMenu1Page, subMenu1CursorPosition, maxSubMenu1Pages, subMenu1ItemsSize);
            Serial.print("buttonstate1: ");
            Serial.println(buttonState1);

        } else if (buttonState1) {
            menuDown(subMenu1Page, subMenu1CursorPosition, maxSubMenu1Pages, subMenu1ItemsSize);
            Serial.print("buttonstate1: ");
            Serial.println(buttonState1);

        } 

        menuDraw(subMenu1Items, subMenu1ItemsSize, subMenu1Page, maxSubMenu1Pages);
        drawCursor(subMenu1Page, subMenu1CursorPosition);
        buttonState1 = false;
        buttonState2 = false;
        buttonState3 = false;
        Serial.println("MenuItem1");
        Serial.println(subMenu1Page);
        updateDisplay();
    }
}

void menuUp(int& menuPage, int& cursorPosition, int maxMenuPages, int menuItemsSize) {
    if (menuPage == 0) {
        cursorPosition = cursorPosition - 1;
        cursorPosition = constrain(cursorPosition, 0, (menuItemsSize - 1));
        Serial.print("Menu up cursor position: ");
        Serial.println(cursorPosition);
    }
    if (menuPage % 2 == 0 && cursorPosition % 2 == 0) {
        menuPage = menuPage - 1;
        menuPage = constrain(menuPage, 0, maxMenuPages);
    }

    if (menuPage % 2 != 0 && cursorPosition % 2 != 0) {
        menuPage = menuPage - 1;
        menuPage = constrain(menuPage, 0, maxMenuPages);
    }

    cursorPosition = cursorPosition - 1;
    cursorPosition = constrain(cursorPosition, 0, (menuItemsSize - 1));
    Serial.print("cursorPosition: ");
    Serial.println(cursorPosition);
}

void menuDown(int& menuPage, int& cursorPosition, int maxMenuPages, int menuItemsSize) {
    if (menuPage % 2 == 0 && cursorPosition % 2 != 0) {
        menuPage = menuPage + 1;
        menuPage = constrain(menuPage, 0, maxMenuPages);
    }

    if (menuPage % 2 != 0 && cursorPosition % 2 == 0) {
        menuPage = menuPage + 1;
        menuPage = constrain(menuPage, 0, maxMenuPages);
    }

    cursorPosition = cursorPosition + 1;
    cursorPosition = constrain(cursorPosition, 0, (menuItemsSize - 1));
    Serial.print("cursorPosition: ");
    Serial.println(cursorPosition);
}

void button32_callback() {
    if (millis() - last_call_int > 300) {
        int tmp2 = digitalRead(button2_pin);
        if (tmp2 == LOW) {
            buttonState1 = !buttonState1;
        } else {
            buttonState2 = !buttonState2;
        }
        last_lcd_backlight = millis();
    }
    last_call_int = millis();
}

void button1_callback() {
    if (millis() - last_call_int3 > 300) {
        buttonState3 = !buttonState3;
        last_lcd_backlight = millis();
    }
    last_call_int3 = millis();
}

String getNodeName(const byte node[]) {
    String result = "";
    for (int i = 0; i < node_name_length; i++) {
        result.concat(char(node[i]));
    }
    return result;
}