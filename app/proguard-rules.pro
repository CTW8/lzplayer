# Add project specific ProGuard rules here.
# You can control the set of applied configuration files using the
# proguardFiles setting in build.gradle.
#
# For more details, see
#   http://developer.android.com/guide/developing/tools/proguard.html

# If your project uses WebView with JS, uncomment the following
# and specify the fully qualified class name to the JavaScript interface
# class:
#-keepclassmembers class fqcn.of.javascript.interface.for.webview {
#   public *;
#}

# Uncomment this to preserve the line number information for
# debugging stack traces.
#-keepattributes SourceFile,LineNumberTable

# If you keep the line number information, uncomment this to
# hide the original source file name.
#-renamesourcefileattribute SourceFile

# Keep all inner classes to avoid DEX compilation issues
-keep class com.example.lzplayer.MainActivity$* { *; }
-keepclassmembers class com.example.lzplayer.MainActivity$* { *; }

# Keep all callback classes
-keep class * implements androidx.activity.result.ActivityResultCallback { *; }
-keep class * implements android.widget.SeekBar$OnSeekBarChangeListener { *; }
-keep class * implements java.lang.Runnable { *; }

# Prevent obfuscation of classes that might cause DEX issues
-dontobfuscate
-optimizations !code/simplification/arithmetic,!code/simplification/cast,!field/*,!class/merging/*