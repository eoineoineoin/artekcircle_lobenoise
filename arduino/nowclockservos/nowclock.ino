#include <Servo.h> 

struct clock
{
	int m_speed;
	int m_travelTime;
	int m_frequency;
	int m_stopSpeed;

	int m_nextOpTime;
	int m_nextSpeed;

	void init(int speed, int travelTime, int frequency)
	{
		m_speed = speed;
		m_travelTime = travelTime;
		m_frequency = frequency;

		m_stopSpeed = 95;

		m_nextOpTime = 0;
		m_nextSpeed = speed;
	}
};


clock clocks[4];
Servo servos[sizeof(clocks) / sizeof(clocks[0])];

void setup() 
{ 
	Serial.begin(9600);
	servos[0].attach(8);
	servos[1].attach(9);
	servos[2].attach(10);
	servos[3].attach(11);

	//Clock A - on pin 8
	clocks[0].init(90, 440, 1000);

	//Clock B - on pin 9
	clocks[1].init(83, 365, 600);

	//Clock C - on pin 10
	clocks[2].init(88, 145, 550);

	//Clock D - on pin 11
	clocks[3].init(82, 290, 800);

	//This servo is different for some reason:
	clocks[3].m_stopSpeed = 96;

	//Debug
	pinMode(13, OUTPUT);
} 

void loop() 
{ 
	//while(true){ interactiveLoop(); }

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
		static int lastMode = 0;
		digitalWrite(13, lastMode);
		lastMode = !lastMode;

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


	int servoId = 3;
	int off = clocks[3].m_stopSpeed;

	if(speed && time)
	{
		Serial.write("Gotcha\n");
		servos[servoId].write(speed);
		delay(time);
		servos[servoId].write(off);
	}
}
