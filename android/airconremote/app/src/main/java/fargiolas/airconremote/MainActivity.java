package fargiolas.airconremote;

import android.content.res.Resources;
import android.os.Handler;
import android.support.v7.app.AppCompatActivity;
import android.os.Bundle;
import android.support.v7.widget.Toolbar;
import android.text.Html;
import android.util.Log;
import android.view.View;
import android.widget.Button;
import android.widget.CompoundButton;
import android.widget.ImageButton;
import android.widget.TextView;
import android.widget.ToggleButton;

import org.eclipse.paho.android.service.MqttAndroidClient;
import org.eclipse.paho.client.mqttv3.DisconnectedBufferOptions;
import org.eclipse.paho.client.mqttv3.IMqttActionListener;
import org.eclipse.paho.client.mqttv3.IMqttMessageListener;
import org.eclipse.paho.client.mqttv3.IMqttToken;
import org.eclipse.paho.client.mqttv3.MqttConnectOptions;
import org.eclipse.paho.client.mqttv3.MqttException;
import org.eclipse.paho.client.mqttv3.MqttMessage;
import org.eclipse.paho.client.mqttv3.internal.security.SSLSocketFactoryFactory;

import java.util.Date;

import javax.net.ssl.SSLSocketFactory;

public class MainActivity extends AppCompatActivity {
    String server;
    String topic;
    final String LOGTAG = "AirConRemote";
    MqttAndroidClient mqttAndroidClient;
    String client_id = "AirConRemote";

    AirConState ACState;

    Handler handler = new Handler();
    Runnable temperature_obsolete_cb = new Runnable() {
        @Override
        public void run() {
            TextView tv = (TextView) findViewById(R.id.RoomTemptextView);
            tv.setVisibility(View.INVISIBLE);
        }
    };


    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);
        Toolbar toolbar = (Toolbar) findViewById(R.id.my_awesome_toolbar);
        setSupportActionBar(toolbar);
        ACState = new AirConState(this);

        server = getResources().getString(R.string.ssl_server);
        topic = getResources().getString(R.string.command_topic);

        /* temperature widget: a really poor one */
        TextView temp_textview = (TextView) findViewById(R.id.TempTextview);
        temp_textview.setText(Html.fromHtml("<b>" + ACState.get_Temperature() +
                "</b><small> °C</small>"));
        ImageButton temp_up_button = (ImageButton) findViewById(R.id.TempUpButton);
        temp_up_button.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                TextView temp_textview = (TextView) findViewById(R.id.TempTextview);
                int temperature = ACState.set_Temperature(ACState.get_Temperature()+1);
                temp_textview.setText(Html.fromHtml("<b>" + temperature + "</b><small> " +
                        "°C</small>"));
            }
        });
        ImageButton temp_down_button = (ImageButton) findViewById(R.id.TempDownButton);
        temp_down_button.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                TextView temp_textview = (TextView) findViewById(R.id.TempTextview);
                int temperature = ACState.set_Temperature(ACState.get_Temperature()-1);
                temp_textview.setText(Html.fromHtml("<b>" + temperature + "</b><small> °" +
                        "C</small>"));
            }
        });

        /* mode buttons */
        Button mode_button = (Button) findViewById(R.id.ModeButton);
        mode_button_set_label(mode_button, "mode", ACState.get_ACMode());
        mode_button.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                String m = ACState.switch_ACMode();
                mode_button_set_label((Button) v, "mode", m);
            }
        });

        Button fan_button = (Button) findViewById(R.id.FanButton);
        mode_button_set_label(fan_button, "fan", ACState.get_FanMode());
        fan_button.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                String m = ACState.switch_FanMode();
                mode_button_set_label((Button) v, "fan", m);
            }
        });

        /* toggle buttons */
        ToggleButton power_toggle = (ToggleButton) findViewById(R.id.PowerToggleButton);
        power_toggle.setChecked(ACState.get_BoolMode("power"));
        power_toggle.setOnCheckedChangeListener(new CompoundButton.OnCheckedChangeListener() {
            @Override
            public void onCheckedChanged(CompoundButton buttonView, boolean isChecked) {
                ACState.set_BoolMode("power", isChecked);
                /* this I hate so much, samsung implements power as a state like everything else
                 but people expects the button to be a toggle so it gets really annoying when
                 state is not in sync, I could probably implement a delayed send for each command
                 though */
                mqtt_send_command();
            }
        });

        ToggleButton display_toggle = (ToggleButton) findViewById(R.id.DisplayToggleButton);
        display_toggle.setChecked(ACState.get_BoolMode("display"));
        display_toggle.setOnCheckedChangeListener(new CompoundButton.OnCheckedChangeListener() {
            @Override
            public void onCheckedChanged(CompoundButton buttonView, boolean isChecked) {
                ACState.set_BoolMode("display", isChecked);
            }
        });

        ToggleButton swing_toggle = (ToggleButton) findViewById(R.id.SwingToggleButton);
        swing_toggle.setChecked(ACState.get_BoolMode("swing"));
        swing_toggle.setOnCheckedChangeListener(new CompoundButton.OnCheckedChangeListener() {
            @Override
            public void onCheckedChanged(CompoundButton buttonView, boolean isChecked) {
                ACState.set_BoolMode("swing", isChecked);
            }
        });

        ToggleButton turbo_toggle = (ToggleButton) findViewById(R.id.TurboToggleButton);
        turbo_toggle.setChecked(ACState.get_BoolMode("turbo"));
        turbo_toggle.setOnCheckedChangeListener(new CompoundButton.OnCheckedChangeListener() {
            @Override
            public void onCheckedChanged(CompoundButton buttonView, boolean isChecked) {
                ACState.set_BoolMode("turbo", isChecked);
            }
        });

        ImageButton send_button = (ImageButton) findViewById(R.id.SendButton);
        send_button.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                mqtt_send_command();
            }
        });

        mqttAndroidClient = new MqttAndroidClient(getApplicationContext(), server, client_id);
        client_id = client_id + System.currentTimeMillis();

        MqttConnectOptions mqttConnectOptions = new MqttConnectOptions();

        mqttConnectOptions.setAutomaticReconnect(true);
        mqttConnectOptions.setCleanSession(true);
        SslToolbox sslToolbox = SslToolbox.getSslToolbox();
        SSLSocketFactory factory = sslToolbox.getSocketFactoryFromRes(this,
                R.raw.trust_keystore, R.string.ca_keystore_password,
                R.raw.android_store_p12, R.string.key_keystore_password);
        mqttConnectOptions.setSocketFactory(factory);
        try {
            //addToHistory("Connecting to " + serverUri);
            mqttAndroidClient.connect(mqttConnectOptions, null, new IMqttActionListener() {
                @Override
                public void onSuccess(IMqttToken asyncActionToken) {
                    DisconnectedBufferOptions disconnectedBufferOptions = new DisconnectedBufferOptions();
                    disconnectedBufferOptions.setBufferEnabled(true);
                    disconnectedBufferOptions.setBufferSize(100);
                    disconnectedBufferOptions.setPersistBuffer(false);
                    disconnectedBufferOptions.setDeleteOldestMessages(false);
                    mqttAndroidClient.setBufferOpts(disconnectedBufferOptions);
                    Log.w(LOGTAG, "Connected to MQTT Server");

                    mqtt_subscribe();
                }

                @Override
                public void onFailure(IMqttToken asyncActionToken, Throwable exception) {
                    Log.w(LOGTAG, "Failed to connect to: " + server + exception.getMessage());
                }
            });
        } catch (MqttException ex) {
            ex.printStackTrace();
        }
    }

    public void mqtt_send_command() {
        String payload = ("power=" + ACState.get_BoolModeStr("power") +
                ", mode=" + ACState.get_ACMode() +
                ", fan=" + ACState.get_FanMode() +
                ", swing=" + ACState.get_BoolModeStr("swing") +
                ", display=" + ACState.get_BoolModeStr("display") +
                ", energy=" + (ACState.get_BoolMode("turbo") ? "turbo" : "normal") +
                ", temperature=" + ACState.get_Temperature());

        Log.w(LOGTAG, "Payload: " + payload);

        try {
            MqttMessage message = new MqttMessage(payload.getBytes());
            mqttAndroidClient.publish(topic, message);
        } catch (MqttException ex) {
            Log.w(LOGTAG, "Failed publishing: " + ex.getMessage());
        }

    }

    public void mode_button_set_label(Button button, String name, String mode) {
        button.setText(Html.fromHtml("<small>" + name.toUpperCase() + "</small><br/>" +
                "<big><b>" + mode.toUpperCase() + "</b></big>"));
    }

    public void mqtt_subscribe(){
        try {
            mqttAndroidClient.subscribe("/samsungac/temperature", 0, null, new
                    IMqttActionListener() {
                @Override
                public void onSuccess(IMqttToken asyncActionToken) {
                    Log.w(LOGTAG, "Subscribed to temperature notifications");
                }

                @Override
                public void onFailure(IMqttToken asyncActionToken, Throwable exception) {
                    Log.w(LOGTAG, "Failed to subscribe to temperature notifications");
                }
            });

            mqttAndroidClient.subscribe("/samsungac/temperature", 0, new IMqttMessageListener() {
                @Override
                public void messageArrived(String topic, MqttMessage message) throws Exception {
                    final String msg = new String(message.getPayload());
                    Log.w(LOGTAG, "Temperature:" + msg);

                    handler.post(new Runnable() {
                        @Override
                        public void run() {
                            TextView tv = (TextView) findViewById(R.id.RoomTemptextView);
                            tv.setText("room temperature: " + msg +" °C");
                            tv.setVisibility(View.VISIBLE);
                        }
                    });

                    /* obsolete sensor data after some time, 30s here but can be longer */
                    handler.removeCallbacks(temperature_obsolete_cb);
                    handler.postDelayed(temperature_obsolete_cb, 30000);

                }
            });
        } catch (MqttException ex){
            System.err.println("Exception whilst subscribing");
            ex.printStackTrace();
        }
    }

}