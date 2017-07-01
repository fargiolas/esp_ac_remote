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
import android.content.res.Resources;

import java.security.KeyStore;

import javax.net.ssl.KeyManagerFactory;
import javax.net.ssl.SSLContext;
import javax.net.ssl.SSLSocketFactory;
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
