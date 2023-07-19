<?php
plugin_header();

$sc     =   (strpos($PAGE, 'sc_') === 0);
$m      =   (strpos($PAGE, '_mono') > 0) ? 'm' : 's';
$cc     =   ($m == 'm') ? 'mono' : 'stereo';
?>

<p>
	This plugin introduces a multiband limiter with flexible configuration. In most cases it acts as a brick-wall limiter but
	there are several settings for which is acts as an compressor with extreme settings, so the output signal may exceed the limiter's threshold. 
	It prevents input <?= ($m) ? 'mono' : 'stereo' ?>signal
	from raising over the specified <b>Threshold</b>. <?php if ($sc) {?> Additional sidechain inputs are provided for better use.<?php } ?>
</p>
<p><u>Attention:</u> this plugin implements set of limiting modes, most of them are iterative. That means that CPU load may be not stable, in other
words: the more work should be done, the more CPU resources will be used. Beware from extreme settings.</p>
<p>The gain reduction algorithm consists of four stages:</p>
<ul>
    <li>Per frequency band automated level regulation.</li>
    <li>Per frequency band brickwall gain reduction.</li>
    <li>Per mix automated level regulation.</li>
    <li>Per mix band brickwall gain reduction.</li>
</ul>
<p>That means that plugin provides per frequency band limiting algorithm and post-limiting after all frequency bands become summed together.</p>
<ul>
    <li>
    	<b>Automated Level Regulation</b> (<b>ALR</b>) acts like a compressor with infinite ratio 
    	for purpose of estimating the smoothed gain reduction level. The gain reduction level is controlled 
    	by the envelope of the signal applied to the compressor's gain reduction curve. The smoothness of the 
    	envelope is controlled by the <b>Attack</b> and <b>Release</b> knobs in the <b>ALR</b> section. Since 
    	different signals have different envelopes with short release time, the <b>Knee</b> knob allows to adjust 
    	the threshold of the compressor. Adjusting <b>Knee</b> may give possibility to gain some additional decibels
    	of loudness in the final result.
    </li>
    <li>
        Peak cutting algorithm which searches peaks above the threshold values and applies short gain reduction patches
        to the signal. The form of patch is controlled by the <b>Mode</b> selector and it's length is controlled by 
        the corresponding <b>Attack</b> and <b>Release</b> knobs. Note that <b>Lookahead</b> also affects these values:
        <b>Attack</b> can not be larger than <b>Lookahead</b> and <b>Release</b> can not be twice larger as <b>Lookahead</b>.
        Setting these values larger than allowed automatically makes them considered to be set to maximum possible values.
    </li>
</ul>

<p>Simplified peak processing example is shown on the following picture:</p>
<?php out_image('graph/limiter-reduction', 'Simplified peak processing example') ?>
<p>
	Of course, the output signal does not repeat the envelope form of input signal because it's amplitude is changed 
	smoothly, so in fact the form of output signal is more complicated.
</p> 
<p>
	Currently there are three forms of patches applied to the gain curve -
	<b>hermite</b> (using cubic polynom for interpolation transients), <b>exponential</b> and <b>linear</b>.
	These forms can be explained with following picture:
</p>
<?php out_image('graph/limiter-patches', 'Forms of patches applied to signal') ?>
<p>
	Gain reduction patch affects not only the peak sample, but also surrounding samples.
	The position and form of this interpolation is related to the peak, so there are four 
	different variants of patch envelope - <b>thin</b>, <b>tail</b>, <b>duck</b> and <b>wide</b>.
	All these forms related to the peak are shown on the following picture:
</p>
<?php out_image('graph/limiter-envelope', 'Envelope forms of the patch') ?>
<p>
	On this image, sloping lines mean the transision part of the patch.
	The flat cap in the middle before the peak is a half of attack time, the flat cap in the middle after the peak is a half of release time.
	Also it's obvious that different envelope forms differently affect dynamics of the signal.
</p>

<p><b>Controls:</b></p>
<ul>
	<li>
		<b>Bypass</b> - bypass switch, when turned on (led indicator is shining), the plugin bypasses signal.
	</li>
	<li><b>SC Boost</b> - enables addidional boost of the sidechain signal:</li>
	<ul>
		<li><b>None</b> - no sidechain boost is applied.</li>
		<li><b>Pink BT</b> - a +3db/octave sidechain boost using bilinear-transformed shelving filter.</li>
		<li><b>Pink MT</b> - a +3db/octave sidechain boost using matched-transformed shelving filter.</li>
		<li><b>Brown BT</b> - a +6db/octave sidechain boost using bilinear-transformed shelving filter.</li>
		<li><b>Brown MT</b> - a +6db/octave sidechain boost using matched-transformed shelving filter.</li>
	</ul>
	<li><b>Oversampling</b> - oversampling mode:</li>
	<ul>
		<li><b>None</b> - oversampling is not used.</li>
		<li><b>Half 2x/16 bit</b> - 2x Lanczos oversampling of Sidechain signal with 16-bit precision of output samples.</li>
		<li><b>Half 2x/24 bit</b> - 2x Lanczos oversampling of Sidechain signal with 24-bit precision of output samples.</li>
		<li><b>Half 3x/16 bit</b> - 3x Lanczos oversampling of Sidechain signal with 16-bit precision of output samples.</li>
		<li><b>Half 3x/24 bit</b> - 3x Lanczos oversampling of Sidechain signal with 24-bit precision of output samples.</li>
		<li><b>Half 4x/16 bit</b> - 4x Lanczos oversampling of Sidechain signal with 16-bit precision of output samples.</li>
		<li><b>Half 4x/24 bit</b> - 4x Lanczos oversampling of Sidechain signal with 24-bit precision of output samples.</li>
		<li><b>Half 6x/16 bit</b> - 6x Lanczos oversampling of Sidechain signal with 16-bit precision of output samples.</li>
		<li><b>Half 6x/24 bit</b> - 6x Lanczos oversampling of Sidechain signal with 24-bit precision of output samples.</li>
		<li><b>Half 8x/16 bit</b> - 8x Lanczos oversampling of Sidechain signal with 16-bit precision of output samples.</li>
		<li><b>Half 8x/24 bit</b> - 8x Lanczos oversampling of Sidechain signal with 24-bit precision of output samples.</li>
		<li><b>Full 2x/16 bit</b> - 2x Lanczos oversampling of Sidechain signal and Input signal with 16-bit precision of output samples.</li>
		<li><b>Full 2x/24 bit</b> - 2x Lanczos oversampling of Sidechain signal and Input signal with 24-bit precision of output samples.</li>
		<li><b>Full 3x/16 bit</b> - 3x Lanczos oversampling of Sidechain signal and Input signal with 16-bit precision of output samples.</li>
		<li><b>Full 3x/24 bit</b> - 3x Lanczos oversampling of Sidechain signal and Input signal with 24-bit precision of output samples.</li>
		<li><b>Full 4x/16 bit</b> - 4x Lanczos oversampling of Sidechain signal and Input signal with 16-bit precision of output samples.</li>
		<li><b>Full 4x/24 bit</b> - 4x Lanczos oversampling of Sidechain signal and Input signal with 24-bit precision of output samples.</li>
		<li><b>Full 6x/16 bit</b> - 6x Lanczos oversampling of Sidechain signal and Input signal with 16-bit precision of output samples.</li>
		<li><b>Full 6x/24 bit</b> - 6x Lanczos oversampling of Sidechain signal and Input signal with 24-bit precision of output samples.</li>
		<li><b>Full 8x/16 bit</b> - 8x Lanczos oversampling of Sidechain signal and Input signal with 16-bit precision of output samples.</li>
		<li><b>Full 8x/24 bit</b> - 8x Lanczos oversampling of Sidechain signal and Input signal with 24-bit precision of output samples.</li>
	</ul>
	<li><b>Dither</b> - enables some dithering noise for the specified output amplitude quantization bitness.</li>
	<li><b>Zoom</b> - zoom fader, allows to adjust zoom on the frequency chart.</li>
	<?php if ($sc) {?> 
	<li><b>External Sidechain</b> - uses the signal passed to additional sidechain inputs of the plugin as a control signal.</li>
	<?php }?>
	<li><b>Zoom</b> - zoom fader, allows to adjust zoom on the frequency chart.</li>
	<li><b>In</b> - the input signal meter.</li>
	<li><b>Out</b> - the output signal meter.</li>
</ul>
<p><b>'Signal' section:</b></p>
<ul>
	<li><b>Input</b> - the amount of gain applied to the input signal before processing.</li>
	<li><b>Output</b> - the amount of gain applied to the output signal before processing.</li>
</ul>
<p><b>'Analysis' section:</b></p>
<ul>
	<li><b>FFT In</b> - enables FFT curve graph of input signal on the spectrum graph.</li>
	<li><b>FFT Out</b> - enables FFT curve graph of output signal on the spectrum graph.</li>
	<li><b>Reactivity</b> - the reactivity (smoothness) of the spectral analysis.</li>
	<li><b>Shift</b> - allows to adjust the overall gain of the analysis.</li>
</ul>
<p><b>'Limiter bands' section:</b></p>
<ul>
	<li><b>Controls</b> - overall controls over the band:</li>
	<ul>
		<li><b>ON</b> - enables the specific band.</li>
		<li><b>ACT</b> - enables the limiter on the specific band and can be used for quick A/B test for the specific band.</li>
		<li><b>ALR</b> - enables ALR (Automatic Level Regulation) for the specific band.</li>
		<li><b>S</b> - enables solo mode for the specific band.</li>
		<li><b>M</b> - mutes the specific band.</li>
	</ul>
	<li><b>Range</b> - the knob that allows to set up the start frequency for corresponding band and view it's frequency range.</li>
	<li><b>ALR</b> - ALR (Automatic Level Regulation) settings for the specific band:</li>
	<ul>
		<li><b>Attack</b> - the attack time of the ALR compressor.</li>
		<li><b>Release</b> - the release time of the ALR compressor.</li>
		<li><b>Knee</b> - the knee of the ALR compressor.</li>
	</ul>
	<li><b>Limiter</b> - limiter settings for the specific band:</li>
	<ul>
		<li><b>Preamp</b> - the pre-amplification of the sidechain signal.</li>
		<li><b>Mode</b> - the operating mode of the specific limiter:</li>
		<ul>
			<li><b>Herm Thin</b>, <b>Herm Wide</b>, <b>Herm Tail</b>, <b>Herm Duck</b> - hermite-interpolated cubic functions are used to apply gain reduction.</b>
			<li><b>Exp Thin</b>, <b>Exp Wide</b>, <b>Exp Tail</b>, <b>Exp Duck</b> - exponent-interpolated functions are used to apply gain reduction.</b>
			<li><b>Line Thin</b>, <b>Line Wide</b>, <b>Line Tail</b>, <b>Line Duck</b> - linear-interpolated functions are used to apply gain reduction.</b>
		</ul>
		<li><b>Boost</b> - applies corresponding to the <b>Threshold</b> gain to the band's output signal.</li>
		<li><b>Thresh</b> - the maximum input level of the signal allowed by limiter.</li>
		<li><b>Attack</b> - the attack time of the limiter. Can not be greater than Lookahead time (greater values are truncated) for some modes.</li>
		<li><b>Release</b> - the attack time of the limiter. Can not be twice greater than Lookahead time (greater values are truncated) for some modes.</li>
		<?php if (!$m) {?> 
		<li><b>Stereo link</b> - stereo link, the degree of mutual influence between gain reduction of stereo channels</li>
		<?php } ?>
		<li><b>Makeup</b> - the makeup gain applied after the limiter stage.</li>
		<li><b>Reduction</b> - the overall gain reduction meter.</li>
	</ul>
</ul>

<p><b>'Output Limiter' section:</b></p>
<ul>
	<li><b>ON</b> - Enables the output limiter.</li>
	<li><b>ALR</b> - Enables the ALR (Automatic Level Regulation) for the output limiter:</li>
	<ul>
		<li><b>Attack</b> - the attack time of the ALR compressor.</li>
		<li><b>Release</b> - the release time of the ALR compressor.</li>
		<li><b>Knee</b> - the knee of the ALR compressor.</li>
	</ul>
	<li><b>Lookahead</b> - the size of lookahead buffer in milliseconds. Forces the limiter to add the corresponding latency to output signal.</li>
	<li><b>Mode</b> - the operating mode of the output limiter:</li>
	<ul>
		<li><b>Herm Thin</b>, <b>Herm Wide</b>, <b>Herm Tail</b>, <b>Herm Duck</b> - hermite-interpolated cubic functions are used to apply gain reduction.</b>
		<li><b>Exp Thin</b>, <b>Exp Wide</b>, <b>Exp Tail</b>, <b>Exp Duck</b> - exponent-interpolated functions are used to apply gain reduction.</b>
		<li><b>Line Thin</b>, <b>Line Wide</b>, <b>Line Tail</b>, <b>Line Duck</b> - linear-interpolated functions are used to apply gain reduction.</b>
	</ul>
	<li><b>Boost</b> - applies corresponding to the <b>Threshold</b> gain to the band's output signal.</li>
	<li><b>Threshold</b> - the maximum output level of the signal allowed by the limiter.</li>
	<li><b>Attack</b> - the attack time of the limiter. Can not be greater than Lookahead time (greater values are truncated) for some modes.</li>
	<li><b>Release</b> - the attack time of the limiter. Can not be twice greater than Lookahead time (greater values are truncated) for some modes.</li>
	<?php if (!$m) {?> 
	<li><b>Stereo link</b> - stereo link, the degree of mutual influence between gain reduction of stereo channels</li>
	<?php } ?>
</ul>