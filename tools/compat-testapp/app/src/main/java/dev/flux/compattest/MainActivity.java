package dev.flux.compattest;

import android.app.Activity;
import android.opengl.GLES20;
import android.opengl.GLSurfaceView;
import android.os.Build;
import android.os.Bundle;
import android.util.Log;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

import java.io.File;
import java.io.FileOutputStream;
import java.io.FileReader;
import java.io.BufferedReader;
import java.nio.charset.StandardCharsets;

import javax.microedition.khronos.egl.EGLConfig;
import javax.microedition.khronos.opengles.GL10;

/** Prints what this process can read about the device; see ../../../../../../README.md. */
public class MainActivity extends Activity {
    static { System.loadLibrary("fluxcompattest"); }

    private native String nativeProbe(String v, String r, String ver);
    private native String nativeGl();

    private TextView out;
    private volatile String glJson = "{}";
    private volatile String javaGl = "{}";

    @Override
    protected void onCreate(Bundle b) {
        super.onCreate(b);
        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        Button refresh = new Button(this);
        refresh.setText("Refresh");
        out = new TextView(this);
        out.setTextIsSelectable(true);
        ScrollView sv = new ScrollView(this);
        sv.addView(out);
        // A 1x1 GL surface so a real context exists for glGetString.
        GLSurfaceView gl = new GLSurfaceView(this);
        gl.setEGLContextClientVersion(2);
        gl.setRenderer(new GLSurfaceView.Renderer() {
            public void onSurfaceCreated(GL10 g, EGLConfig c) {
                javaGl = "{\"vendor\":\"" + q(GLES20.glGetString(GLES20.GL_VENDOR)) + "\",\"renderer\":\""
                        + q(GLES20.glGetString(GLES20.GL_RENDERER)) + "\",\"version\":\"" + q(GLES20.glGetString(GLES20.GL_VERSION)) + "\"}";
                glJson = nativeGl();
                runOnUiThread(MainActivity.this::report);
            }
            public void onSurfaceChanged(GL10 g, int w, int h) {}
            public void onDrawFrame(GL10 g) {}
        });
        root.addView(gl, new LinearLayout.LayoutParams(1, 1));
        root.addView(refresh);
        root.addView(sv, new LinearLayout.LayoutParams(-1, -1));
        setContentView(root);
        refresh.setOnClickListener(v -> report());
        report();
    }

    private static String q(String s) { return s == null ? "" : s.replace("\\", "\\\\").replace("\"", "\\\""); }

    private String cpuHardware() {
        try (BufferedReader r = new BufferedReader(new FileReader("/proc/cpuinfo"))) {
            String l;
            while ((l = r.readLine()) != null) if (l.startsWith("Hardware")) return l;
        } catch (Exception ignored) {}
        return "";
    }

    private void report() {
        StringBuilder j = new StringBuilder("{");
        j.append("\"package\":\"").append(getPackageName()).append("\",\"pid\":").append(android.os.Process.myPid());
        j.append(",\"build\":{");
        j.append("\"BRAND\":\"").append(q(Build.BRAND)).append("\",\"MANUFACTURER\":\"").append(q(Build.MANUFACTURER)).append("\"");
        j.append(",\"MODEL\":\"").append(q(Build.MODEL)).append("\",\"DEVICE\":\"").append(q(Build.DEVICE)).append("\"");
        j.append(",\"PRODUCT\":\"").append(q(Build.PRODUCT)).append("\",\"FINGERPRINT\":\"").append(q(Build.FINGERPRINT)).append("\"");
        j.append(",\"HARDWARE\":\"").append(q(Build.HARDWARE)).append("\",\"BOARD\":\"").append(q(Build.BOARD)).append("\"");
        if (Build.VERSION.SDK_INT >= 31) {
            j.append(",\"SOC_MODEL\":\"").append(q(Build.SOC_MODEL)).append("\",\"SOC_MANUFACTURER\":\"").append(q(Build.SOC_MANUFACTURER)).append("\"");
        }
        j.append("},\"cpuinfo\":\"").append(q(cpuHardware())).append("\"");
        j.append(",\"gl_java\":").append(javaGl).append(",\"gl_native\":").append(glJson);
        j.append(",\"native\":").append(nativeProbe("", "", ""));
        j.append("}");
        String s = j.toString();
        out.setText(s);
        Log.i("FLUXTEST", s);
        try (FileOutputStream f = new FileOutputStream(new File(getFilesDir(), "identity.json"))) {
            f.write(s.getBytes(StandardCharsets.UTF_8));
        } catch (Exception e) {
            Log.w("FLUXTEST", "could not write identity.json: " + e);
        }
    }
}
