import hypermedia.net.*;

UDP socket;
int[] last_buffer, prev_buffer;
int age = 0, prev_age = 0, hud_age = 0;
int drawMode = 0;
int transformMode = 0;

  public void receive(byte[] data, String host, int port)
  {
    int len = data.length / 2;
    int[] buf = new int[len];
    for (int i = 0; i < len; i ++)
    {
      int word = (data[2 * i] & 0xFF) + 256 * (data[2 * i + 1] & 0xFF);
      if (word > 32767)
        word -= 65536;
      buf[i] = word;
    }
    prev_buffer = last_buffer;
    last_buffer = buf;
    prev_age = age;
    age = 0;
  }
  public void timeout()
  {
    text("timeout", 100, 150);
  }

int valueToPixelY(int value)
{
  value *= 8; // a little bit of gain
  return value / 128 + 256; 
}

public class WaveletTransform
{
  public int src[];
  public int lowpass[], highpass[];
  protected float kernel[];
  public void oneStep()
  {
    lowpass = new int[src.length / 2];
    highpass = new int[src.length / 2];
    for(int i = 0; i < src.length; i += 2)
    {
      float lp = 0, hp = 0;
      int koffs = kernel.length / 2;
      for (int k = 0; k < kernel.length; k++)
      {
        int ii = i + k - koffs;
        if (ii >= 0 && ii < src.length)
        { 
          float coeff = src[ii] * kernel[k];
          lp += coeff;
          hp += ((k & 1) == 1 ? +1 : -1) * coeff;
        }
      }
      lowpass[i / 2] = (int)lp;
      highpass[i / 2] = (int)hp;
    }
    src = lowpass;
  }
};

public class HaarTransform extends WaveletTransform
{
  HaarTransform()
  {
    kernel = new float[] { 1, 1 };
  }
};

public class Daubechies4Transform extends WaveletTransform
{
  Daubechies4Transform()
  {
    // http://en.wikipedia.org/wiki/Daubechies_wavelet
    kernel = new float[] { 0.6830127, 1.1830127, 0.3169873, -0.1830127 };
  }
};

public class Daubechies8Transform extends WaveletTransform
{
  Daubechies8Transform()
  {
    // http://en.wikipedia.org/wiki/Daubechies_wavelet
    kernel = new float[] { 0.32580343, 1.01094572, 0.8922014, -0.03957503, -0.26450717, 0.0436163, 0.0465036, -0.01498699};
  }
};

public class Daubechies12Transform extends WaveletTransform
{
  Daubechies12Transform()
  {
    // http://en.wikipedia.org/wiki/Daubechies_wavelet
    kernel = new float[] { 0.15774243, 0.69950381, 1.06226376,    0.44583132, -0.31998660, -0.18351806, 0.13788809, 0.03892321, -0.04466375, 7.83251152e-4, 6.75606236e-3, -1.52353381e-3};
  }
};

class Point
{
  public float x, y;
  Point(float _x, float _y)
  {
    x = _x; y = _y;
  }
};

Point getPointLine(int buffer[], int graph, int pos, int ptidx)
{
  int y;
  if (graph == 1)
    y = -160;
  else
    y = -96 + 64 * graph;
  int step = displayWidth / buffer.length;
  return new Point((pos - 1 + ptidx) * step, y + valueToPixelY(buffer[(pos + buffer.length - 1 + ptidx) % buffer.length]));   
}

Point getPointCircle(int buffer[], int graph, int pos, int ptidx)
{
  int step = displayWidth / buffer.length;
  float angle = (pos - 1 + ptidx) * 2 * PI / buffer.length; 
  float dpoint = valueToPixelY(buffer[(pos + buffer.length - 1 + ptidx) % buffer.length]);
  float radius = 100.0 + 0.5 * dpoint;
  float x = displayWidth / 2 + radius * cos(angle);
  float y = 700   /2 + radius * sin(angle);
  return new Point(x, y);   
}

Point getPointFib(int buffer[], int graph, int pos, int ptidx)
{
  int step = displayWidth / buffer.length;
  float angle = (pos - 1 + ptidx) * 7  * PI / buffer.length; 
  float dpoint = valueToPixelY(buffer[(pos + buffer.length - 1 + ptidx) % buffer.length]);
  float radius = 10 * (2 + graph) * exp(0.25 * 0.306349 * angle) + 0.25 * dpoint;  
  float x = displayWidth / 2 + radius * cos(angle);
  float y = 700   /2 + radius * sin(angle);
  return new Point(x, y);   
}

Point getPoint(int buffer[], int graph, int pos, int ptidx)
{
  if (drawMode == 0)
    return getPointLine(buffer, graph, pos, ptidx); 
  if (drawMode == 1)
    return getPointCircle(buffer, graph, pos, ptidx);
  if (drawMode == 2)
    return getPointFib(buffer, graph, pos, ptidx);
  return null;  
}

void drawSingleGraph(int graph, int buffer[])
{  
  noFill();
  for (int i = 0; i < buffer.length; i++)
  {
    //line(i * step, valueToPixelY(buffer[i]), (i + 1) * step, valueToPixelY(buffer[(i + 1) % buffer.length]));
    Point p1 = getPoint(buffer, graph, i, 0); 
    Point p2 = getPoint(buffer, graph, i, 1); 
    Point p3 = getPoint(buffer, graph, i, 2); 
    Point p4 = getPoint(buffer, graph, i, 3); 
    curve(p1.x, p1.y, p2.x, p2.y, p3.x, p3.y, p4.x, p4.y);
  }
}

String getCell(int i)
{
  if (i < 4 * age && last_buffer != null && i < last_buffer.length)
    return "" + last_buffer[i];
      
  if (prev_buffer != null && i < prev_buffer.length)
    return "" + prev_buffer[i];
   
  return "";
}

void keyPressed()
{
  hud_age = 0;
  if (key == ' ')
    drawMode = (drawMode + 1) % 3;
  if (key == 't')
    transformMode = (transformMode + 1) % 3;
}

void draw()
{
  if (last_buffer != null)
  {
    int alpha = 255 - 4 * age;
    if (alpha < 0)
      alpha = 0;
    background(0);
    textSize(12);
    int spacing = 64;
    int col = 1024 / spacing;
    for (int i = 0; i < 64; ++i)
    {
      int aage = i < 4 * age ? age : prev_age;
      int aalpha = 255 - 4 * aage;
      if (aalpha < 0)
        aalpha = 0;
      //fill(120, 120, 120, aalpha);
      fill(255, 255, 255, aalpha);
      String t = getCell(i);
      if (i >= 4 * age - 3 && i < 4 * age)
        t += "_";  
      text(t, (i % col) * spacing, 700   + 20 * (i / col));
    }
    WaveletTransform ht;
    switch (transformMode)
    {
      case 0:
        ht = new Daubechies4Transform();
        break;
      case 1:
        ht = new Daubechies8Transform();
        break;
      default:
        ht = new HaarTransform();
    }
    if (hud_age < 255)
    {
      final String modeNames[] = new String[] { "Lines", "Circles", "Fibonacci" };  
      fill(255, 255, 255, 255 - hud_age);
      text("Filter: " + ht.getClass().getSimpleName(), 0, 10);
      text("Mode: " + modeNames[drawMode], 0, 30);
    }
    ht.src = last_buffer;
    // Draw the original waveform
    stroke(255, 255, 255, alpha); // all bands
    drawSingleGraph(-1, ht.src);
    // Apply the highpass/lowpass split, draw highpass part in a unique colour and use
    // the lowpass as the input for the next iteration
    for (int step = 1; step < 7; step++)
    {
      switch(step)
      {
        case 1: stroke(50, 50, 50, alpha); break; // 128 Hz - noise - grey
        case 2: stroke(50, 50, 50, alpha); break; // 64 Hz - noise - grey
        case 3: stroke(3, 135, 92, alpha); break; // 32 Hz - gamma + noise - turquoise
        case 4: stroke(203, 72, 5, alpha); break; // 16 Hz - beta - orange
        case 5: stroke(32, 23, 141, alpha); break; // 8 Hz - alpha - dark blue
        case 6: stroke(203, 158, 5, alpha); break; // 4 Hz - theta - yellow
      }
      //stroke((step & 1) > 0 ? 255 : 0, (step & 2) > 0 ? 255 : 0, (step & 4) > 0 ? 255 : 0);
      ht.oneStep();
      drawSingleGraph(step, ht.highpass);
    }
    age++;
    hud_age += 4;
    //println(last_buffer);
  }
}

void setup()
{
  size(displayWidth, displayHeight);
  socket = new UDP(this, 9999, "127.0.0.1");
  socket.setTimeoutHandler("timeout");
  //socket.log(true);
  socket.listen(true);
  stroke(255);
  hud_age = 0;
}

boolean sketchFullScreen() {
  return true;
}


void loop()
{
}

