package fargiolas.airconremote;

import android.content.Context;
import android.content.res.Resources;

import java.security.KeyStore;

import javax.net.ssl.KeyManagerFactory;
import javax.net.ssl.SSLContext;
import javax.net.ssl.SSLSocketFactory;
import javax.net.ssl.TrustManager;
import javax.net.ssl.TrustManagerFactory;

public class SslToolbox {
    private static SslToolbox instance = null;

    private SslToolbox() {}

    public static SslToolbox getSslToolbox() {
        if (instance == null) {
            synchronized (SslToolbox.class) {
                if (instance == null) instance = new SslToolbox();
            }
        }
        return instance;
    }

    public SSLSocketFactory getSocketFactoryFromRes(
            Context context,
            int caStoreId, int caPassId,
            int keyStoreId, int keyPassId) {

        Resources r = context.getResources();
        SSLSocketFactory factory = null;

        try {
            KeyStore trustKS = KeyStore.getInstance("BKS");
            trustKS.load(r.openRawResource(caStoreId), r.getString(caPassId).toCharArray());
            TrustManagerFactory tmf = TrustManagerFactory.getInstance(TrustManagerFactory
                    .getDefaultAlgorithm());
            tmf.init(trustKS);

            KeyStore keyKS = KeyStore.getInstance("PKCS12");
            keyKS.load(r.openRawResource(keyStoreId), r.getString(keyPassId).toCharArray());
            KeyManagerFactory kmf = KeyManagerFactory.getInstance(KeyManagerFactory
                    .getDefaultAlgorithm());
            kmf.init(keyKS, null);

            SSLContext sslcontext = SSLContext.getInstance("TLS");
            sslcontext.init(kmf.getKeyManagers(), tmf.getTrustManagers(), null);

            factory = sslcontext.getSocketFactory();
        } catch(Exception e) {
            e.printStackTrace();
        }

    return factory;
    }
}
