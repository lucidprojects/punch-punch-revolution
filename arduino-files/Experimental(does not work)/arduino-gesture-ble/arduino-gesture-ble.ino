#include <SPI.h>
#include <Arduino_LSM9DS1.h>
#include <ArduinoBLE.h>

#include <TensorFlowLite.h>
#include <tensorflow/lite/experimental/micro/kernels/all_ops_resolver.h>
#include <tensorflow/lite/experimental/micro/micro_error_reporter.h>
#include <tensorflow/lite/experimental/micro/micro_interpreter.h>
#include <tensorflow/lite/schema/schema_generated.h>
#include <tensorflow/lite/version.h>

#include "model.h"

BLEService punchService("19b10010-e8f2-537e-4f6c-d104768a1214");

BLEIntCharacteristic punchCharacteristic("6635d693-9ad2-408e-ad48-4d8f88810dee", BLERead | BLENotify);


const float accelerationThreshold = 2.5; // threshold of significant in G's
const int numSamples = 119;

int samplesRead = numSamples;

// global variables used for TensorFlow Lite (Micro)
tflite::MicroErrorReporter tflErrorReporter;

// pull in all the TFLM ops, you can remove this line and
// only pull in the TFLM ops you need, if would like to reduce
// the compiled size of the sketch.
tflite::ops::micro::AllOpsResolver tflOpsResolver;

const tflite::Model* tflModel = nullptr;
tflite::MicroInterpreter* tflInterpreter = nullptr;
TfLiteTensor* tflInputTensor = nullptr;
TfLiteTensor* tflOutputTensor = nullptr;

// Create a static memory buffer for TFLM, the size may need to
// be adjusted based on the model you are using
constexpr int tensorArenaSize = 8 * 1024;
byte tensorArena[tensorArenaSize];

// array to map gesture index to a name
const char* GESTURES[] = {
  "jab",
  "cross",
  "hook",
  "uppercut"
};

#define NUM_GESTURES (sizeof(GESTURES) / sizeof(GESTURES[0]))


void setup() {
  Serial.begin(9600);
//  while (!Serial);

  // initialize the IMU
  if (!IMU.begin()) {
    Serial.println("Failed to initialize IMU!");
    while (1);
  }

  if (!BLE.begin()) {
    Serial.println("starting BLE failed!");
    //    while (1);
  } 
    BLE.setLocalName("Punch");
    BLE.setAdvertisedService(punchService);

    punchService.addCharacteristic(punchCharacteristic);

    BLE.addService(punchService);

    punchCharacteristic.writeValue(100);

    BLE.advertise();

    Serial.println("Bluetooth device active, waiting for connections...");
  

  // print out the samples rates of the IMUs
  Serial.print("Accelerometer sample rate = ");
  Serial.print(IMU.accelerationSampleRate());
  Serial.println(" Hz");
  Serial.print("Gyroscope sample rate = ");
  Serial.print(IMU.gyroscopeSampleRate());
  Serial.println(" Hz");

  Serial.println();

  // get the TFL representation of the model byte array
  tflModel = tflite::GetModel(model);
  if (tflModel->version() != TFLITE_SCHEMA_VERSION) {
    Serial.println("Model schema mismatch!");
    while (1);
  }

  // Create an interpreter to run the model
  tflInterpreter = new tflite::MicroInterpreter(tflModel, tflOpsResolver, tensorArena, tensorArenaSize, &tflErrorReporter);

  // Allocate memory for the model's input and output tensors
  tflInterpreter->AllocateTensors();

  // Get pointers for the model's input and output tensors
  tflInputTensor = tflInterpreter->input(0);
  tflOutputTensor = tflInterpreter->output(0);

}

void loop() {
  float aX, aY, aZ, gX, gY, gZ;

  BLE.poll();

//  BLEDevice central = BLE.central();

  
  // wait for significant motion
  while (samplesRead == numSamples) {
    if (IMU.accelerationAvailable()) {
      // read the acceleration data
      IMU.readAcceleration(aX, aY, aZ);

      // sum up the absolutes
      float aSum = fabs(aX) + fabs(aY) + fabs(aZ);

      // check if it's above the threshold
      if (aSum >= accelerationThreshold) {
        // reset the sample read count
        samplesRead = 0;
        break;
      }
    }
  }

  // check if the all the required samples have been read since
  // the last time the significant motion was detected
  if (samplesRead < numSamples) {
    // check if new acceleration AND gyroscope data is available
    if (IMU.accelerationAvailable() && IMU.gyroscopeAvailable()) {
      // read the acceleration and gyroscope data
      IMU.readAcceleration(aX, aY, aZ);
      IMU.readGyroscope(gX, gY, gZ);

      // normalize the IMU data between 0 to 1 and store in the model's
      // input tensor
      tflInputTensor->data.f[samplesRead * 6 + 0] = (aX + 4.0) / 8.0;
      tflInputTensor->data.f[samplesRead * 6 + 1] = (aY + 4.0) / 8.0;
      tflInputTensor->data.f[samplesRead * 6 + 2] = (aZ + 4.0) / 8.0;
      tflInputTensor->data.f[samplesRead * 6 + 3] = (gX + 2000.0) / 4000.0;
      tflInputTensor->data.f[samplesRead * 6 + 4] = (gY + 2000.0) / 4000.0;
      tflInputTensor->data.f[samplesRead * 6 + 5] = (gZ + 2000.0) / 4000.0;

      samplesRead++;

      if (samplesRead == numSamples) {
        // Run inferencing
        TfLiteStatus invokeStatus = tflInterpreter->Invoke();
        if (invokeStatus != kTfLiteOk) {
          Serial.println("Invoke failed!");
          while (1);
          return;
        }

        //         Loop through the output tensor values from the model
        //        for (int i = 0; i < NUM_GESTURES; i++) {
        //          Serial.print(GESTURES[i]);
        //          Serial.print(": ");
        //          Serial.println(tflOutputTensor->data.f[i], 2);
        //        }


        float jab_value = tflOutputTensor->data.f[0];
        float cross_value = tflOutputTensor->data.f[1];
        float hook_value = tflOutputTensor->data.f[2];
        float uppercut_value = tflOutputTensor->data.f[3];
        float highest_value = 0;
        String string_value = "Hello String";

        if (jab_value > cross_value && jab_value > hook_value && jab_value > uppercut_value) {
          highest_value = jab_value;
          string_value = "This is a jab";
          punchCharacteristic.writeValue(0);
          Serial.println(string_value);
        } else if ( cross_value > jab_value && cross_value > hook_value && cross_value > uppercut_value) {
          highest_value = cross_value;
          string_value = "This is a cross";
          punchCharacteristic.writeValue(1);
          Serial.println(string_value);
        } else if ( hook_value > jab_value && hook_value > cross_value && hook_value > uppercut_value) {
          highest_value = hook_value;
          string_value = "This is a hook";
          Serial.println(string_value);
          punchCharacteristic.writeValue(2);
        } else { 
          highest_value = uppercut_value;
          string_value = "This is a uppercut";
          Serial.println(string_value);
          punchCharacteristic.writeValue(3);
        }

      }
    }
  }
}