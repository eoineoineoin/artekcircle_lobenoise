#include <Servo.h> 

struct clock
{
	int m_speed;
	int m_travelTime;
	int m_frequency;
	int m_stopSpeed;

	int m_nextOpTime;
	int m_nextSpeed;
};


clock clocks[2];
Servo servos[sizeof(clocks) / sizeof(clocks[0])];

void setup() 
{ 
	Serial.begin(9600);
	servos[0].attach(9);
	servos[1].attach(10);

	clocks[0] = {90, 172, 1000, 96, 0, 90};
	clocks[1] = {83, 160, 600, 95, 0, 83};
} 

void loop() 
{ 
	while(true)
	{
		int nextToChange = 0;
		for(int c = 0; c < sizeof(clocks) / sizeof(clocks[0]); c++)
		{
			if(clocks[c].m_nextOpTime < clocks[nextToChange].m_nextOpTime)
			{
				nextToChange = c;
			}
		}

		int ttlNextOp = clocks[nextToChange].m_nextOpTime;
		for(int c = 0; c < sizeof(clocks) / sizeof(clocks[0]); c++)
		{
			clocks[c].m_nextOpTime -= ttlNextOp;
		}
		delay(ttlNextOp);

		servos[nextToChange].write(clocks[nextToChange].m_nextSpeed);
		if(clocks[nextToChange].m_nextSpeed == clocks[nextToChange].m_stopSpeed)
		{
			clocks[nextToChange].m_nextSpeed = clocks[nextToChange].m_speed;
			clocks[nextToChange].m_nextOpTime = clocks[nextToChange].m_frequency - clocks[nextToChange].m_travelTime;
		}
		else
		{
			clocks[nextToChange].m_nextSpeed = clocks[nextToChange].m_stopSpeed;
			clocks[nextToChange].m_nextOpTime = clocks[nextToChange].m_travelTime;
		}
	}
}

void interactiveLoop()
{
	/*
	int speed = Serial.parseInt();

	while(!speed)
	{
		speed = Serial.parseInt();
	}

	
	int time = Serial.parseInt();
	while(!time)
	{
		time = Serial.parseInt();
	}

	int off = 96;

	if(speed && time)
	{
		Serial.write("Gotcha\n");
		servoA.write(speed);
		servoA.write(speed);
		delay(time);
		servoA.write(off);
		servoB.write(off);
	}
	*/
}
