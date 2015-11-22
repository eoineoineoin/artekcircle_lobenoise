#include <Servo.h> 

Servo myservo;

void setup() 
{ 
	Serial.begin(9600);
	myservo.attach(9);
} 

void loop() 
{ 
	int v = Serial.parseInt();
	static int lastVal = 0;
	if(v && v != lastVal)
	{
		Serial.write("Gotcha\n");
		myservo.write(v);
		lastVal = v;
	}
} 
