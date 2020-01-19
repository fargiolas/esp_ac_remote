/*
 *     Baschixedda - a simple android app to control my AC with MQTT
 *     Copyright (C) 2017 Filippo Argiolas <filippo.argiolas@gmail.com>
 *
 *     This program is free software: you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation, either version 3 of the License, or
 *     (at your option) any later version.
 *
 *     This program is distributed in the hope that it will be useful,
 *     but WITHOUT ANY WARRANTY; without even the implied warranty of
 *     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *     GNU General Public License for more details.
 *
 *     You should have received a copy of the GNU General Public License
 *     along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

package fargiolas.airconremote;

import android.content.Context;
import android.util.Log;

import org.eclipse.paho.android.service.MqttAndroidClient;
import org.eclipse.paho.client.mqttv3.DisconnectedBufferOptions;
import org.eclipse.paho.client.mqttv3.IMqttActionListener;
import org.eclipse.paho.client.mqttv3.IMqttDeliveryToken;
import org.eclipse.paho.client.mqttv3.IMqttToken;
import org.eclipse.paho.client.mqttv3.MqttCallback;
import org.eclipse.paho.client.mqttv3.MqttCallbackExtended;
import org.eclipse.paho.client.mqttv3.MqttConnectOptions;
import org.eclipse.paho.client.mqttv3.MqttException;
import org.eclipse.paho.client.mqttv3.MqttMessage;

import java.util.UUID;

import javax.net.ssl.SSLSocketFactory;

public class MqttHelper implements IMqttActionListener, MqttCallbackExtended {
    private final static String LOGTAG = MqttHelper.class.getSimpleName();

    /* FIXME: context as a static field leaks memory... */
    /* singleton */
    private static MqttHelper instance = null;

    public static MqttHelper getInstance(Context context) {
        if (instance == null) {
            synchronized (MqttHelper.class) {
                if (instance == null) instance = new MqttHelper(context);
            }
        }
        return instance;
    }

    private enum State {
        CONNECTING,
        CONNECTED,
        DISCONNECTING,
        DISCONNECTED,
        FAILURE,
        NONE,
    }

    private MqttAndroidClient client;
    private MqttHelperCallback callback;
    private State state = State.NONE;
    private Context ctx;

    private MqttHelper(Context context) {
        ctx = context.getApplicationContext();
    }

    @Override
    public void finalize() throws Throwable {
        try {
            if (client != null) {
                client.unregisterResources();
            }
        } finally {
            super.finalize();
        }
    }

    public void setCallback(MqttHelperCallback cb) {
        callback = cb;
    }

    /* mqtt helpers */
    public void subscribe(String topic) {
        try {
            Log.w(LOGTAG, "subscribing to: " + topic);
            client.subscribe(topic, 0);
        } catch (MqttException e) {
            Log.e(LOGTAG, "subscribe error: ", e);
        }
    }

    public void unsubscribe(String topic) {
        try {
            client.unsubscribe(topic);
        } catch (MqttException e) {
            Log.e(LOGTAG, "unsubscribe error: ", e);
        }
    }

    public void publish(String topic, String payload) {
        try {
            Log.w(LOGTAG, "publishing: " + topic + ": " + payload);
            MqttMessage message = new MqttMessage(payload.getBytes());
            client.publish(topic, message);
        } catch (MqttException e) {
            Log.e(LOGTAG, "failed publishing" + e.getMessage());
        }
    }

    public void connect(String server, String client_id) {
        if ((state != State.CONNECTED) && (client != null)) {
            return;
        }

        String id = client_id + UUID.randomUUID().toString();

        client = new MqttAndroidClient(ctx, server, id);
        client.registerResources(ctx);
        MqttConnectOptions copts = new MqttConnectOptions();
        copts.setAutomaticReconnect(true);
        copts.setCleanSession(true);

        SslToolbox sslToolbox = SslToolbox.getSslToolbox();
        SSLSocketFactory factory = sslToolbox.getSocketFactoryFromRes(ctx,
                R.raw.trust_keystore, R.string.ca_keystore_password,
                R.raw.android_store_p12, R.string.key_keystore_password);
        copts.setSocketFactory(factory);

        client.setCallback(this);

        try {
            Log.w(LOGTAG, "connecting: " + id);
            state = State.CONNECTING;
            client.connect(copts, null, this);
        } catch (MqttException e) {
            Log.e(LOGTAG, "connect error: ", e);
        }
    }

    public void disconnect() {
        try {
            Log.w(LOGTAG, "disconnecting");
            state = State.DISCONNECTING;
            client.disconnect(null, this);
            client.unregisterResources();
            client = null;
        } catch (MqttException e) {
            Log.e(LOGTAG, "disconnect error: ", e);
        }
    }



    /* IMqttActionListener overrides */
    @Override
    public void onSuccess(IMqttToken token) {
        switch (state) {
            case CONNECTING:
                Log.w(LOGTAG, "connection successful");
                /* where did I take this and what does it do? */
                DisconnectedBufferOptions disconnectedBufferOptions = new DisconnectedBufferOptions();
                disconnectedBufferOptions.setBufferEnabled(true);
                disconnectedBufferOptions.setBufferSize(100);
                disconnectedBufferOptions.setPersistBuffer(false);
                disconnectedBufferOptions.setDeleteOldestMessages(false);
                client.setBufferOpts(disconnectedBufferOptions);

                break;
            case DISCONNECTING:
                Log.w(LOGTAG, "disconnection successful");
                break;
            default:
                Log.e(LOGTAG, "unknown action completed");
        }
    }

    @Override
    public void onFailure(IMqttToken token, Throwable throwable) {
        switch(state) {
            case CONNECTING:
                Log.w(LOGTAG, "connection failed");
                break;
            case DISCONNECTING:
                Log.w(LOGTAG, "disconnection failed");
                break;
            default:
                Log.w(LOGTAG, "something failed, you tell me what");
                state = State.FAILURE;
        }
        /* notify user? */
    }

    /* MqttCallback */
    @Override
    public void connectionLost(Throwable throwable) {
        Log.w(LOGTAG, "connection lost");
        state = State.DISCONNECTED;
    }

    @Override
    public void connectComplete(boolean reconnect, String uri) {
        Log.w(LOGTAG, "uri: " + uri + " (reconnect: " + (reconnect ? "yes" : "no") +  ")");
        state = State.CONNECTED;
        subscribe("/samsungac/temperature");
        subscribe("/samsungac/humidity");
    }

    @Override
    public void messageArrived(String topic, MqttMessage message) throws Exception {
        String payload = new String(message.getPayload());
        Log.w(LOGTAG, "received payload: " + topic + ": " + payload);
        callback.onMessageArrived(topic, message);
    }

    @Override
    public void deliveryComplete(IMqttDeliveryToken token) {
        Log.w(LOGTAG, "delivery complete");
    }

    public interface MqttHelperCallback {
        void onMessageArrived(String topic, MqttMessage message);
    }
}
