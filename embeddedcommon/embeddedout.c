//Copyright 2015 <>< Charles Lohr under the ColorChord License.

#include "embeddedout.h"

//uint8_t ledArray[NUM_LIN_LEDS]; //Points to which notes correspond to these LEDs
uint8_t ledOut[NUM_LIN_LEDS*3];

uint16_t ledSpin;
uint16_t ledAmpOut[NUM_LIN_LEDS];
uint8_t ledFreqOut[NUM_LIN_LEDS];
uint8_t ledFreqOutOld[NUM_LIN_LEDS];
bool needsLoad = true;
uint32 lastRotTime = 0;

uint8_t RootNoteOffset;

void UpdateLinearLEDs()
{
	//Source material:
	/*
		extern uint8_t  note_peak_freqs[];
		extern uint16_t note_peak_amps[];  //[MAXNOTES] 
		extern uint16_t note_peak_amps2[];  //[MAXNOTES]  (Responds quicker)
		extern uint8_t  note_jumped_to[]; //[MAXNOTES] When a note combines into another one,
	*/

	//Goal: Make splotches of light that are porportional to the strength of notes.
	//Color them according to value in note_peak_amps2.

	uint8_t i;
	int8_t k;
	uint16_t j, l;
	uint32_t total_size_all_notes = 0;
	int32_t porpamps[MAXNOTES]; //LEDs for each corresponding note.
	uint8_t sorted_note_map[MAXNOTES]; //mapping from which note into the array of notes from the rest of the system.
	uint8_t sorted_map_count = 0;
	uint32_t note_nerf_a = 0;

	for( i = 0; i < MAXNOTES; i++ )
	{
		if( note_peak_freqs[i] == 255 ) continue;
		note_nerf_a += note_peak_amps[i];
	}

	note_nerf_a = ((note_nerf_a * NERF_NOTE_PORP)>>8);


	for( i = 0; i < MAXNOTES; i++ )
	{
		uint16_t ist = note_peak_amps[i];
		uint8_t nff = note_peak_freqs[i];
		if( nff == 255 )
		{
			continue;
		}
		if( ist < note_nerf_a )
		{
			continue;
		}

#if SORT_NOTES
		for( j = 0; j < sorted_map_count; j++ )
		{
			if( note_peak_freqs[ sorted_note_map[j] ] > nff )
			{
				break;
			}
		}
		for( k = sorted_map_count; k > j; k-- )
		{
			sorted_note_map[k] = sorted_note_map[k-1];
		}
		sorted_note_map[j] = i;
#else
#endif
		sorted_note_map[sorted_map_count] = i;
		sorted_map_count++;
	}

#if 0
	for( i = 0; i < sorted_map_count; i++ )
	{
		printf( "%d: %d: %d /", sorted_note_map[i],  note_peak_freqs[sorted_note_map[i]], note_peak_amps[sorted_note_map[i]] );
	}
	printf( "\n" );
#endif

	uint16_t local_peak_amps[MAXNOTES];
	uint16_t local_peak_amps2[MAXNOTES];
	uint8_t  local_peak_freq[MAXNOTES];

	//Make a copy of all of the variables into local ones so we don't have to keep double-dereferencing.
	for( i = 0; i < sorted_map_count; i++ )
	{
		//printf( "%5d ", local_peak_amps[i] );
		local_peak_amps[i] = note_peak_amps[sorted_note_map[i]] - note_nerf_a;
		local_peak_amps2[i] = note_peak_amps2[sorted_note_map[i]];
		local_peak_freq[i] = note_peak_freqs[sorted_note_map[i]];
//		printf( "%5d ", local_peak_amps[i] );
	}
//	printf( "\n" );

	for( i = 0; i < sorted_map_count; i++ )
	{
		uint16_t ist = local_peak_amps[i];
		porpamps[i] = 0;
		total_size_all_notes += local_peak_amps[i];
	}

	if( total_size_all_notes == 0 )
	{
		for( j = 0; j < USE_NUM_LIN_LEDS * 3; j++ )
		{
			ledOut[j] = 0;
		}
		return;
	}

	uint32_t porportional = (uint32_t)(USE_NUM_LIN_LEDS<<16)/((uint32_t)total_size_all_notes);

	uint16_t total_accounted_leds = 0;

	for( i = 0; i < sorted_map_count; i++ )
	{
		porpamps[i] = (local_peak_amps[i] * porportional) >> 16;
		total_accounted_leds += porpamps[i];
	}

	int16_t total_unaccounted_leds = USE_NUM_LIN_LEDS - total_accounted_leds;

	int addedlast = 1;
	do
	{
		for( i = 0; i < sorted_map_count && total_unaccounted_leds; i++ )
		{
			porpamps[i]++; total_unaccounted_leds--;
			addedlast = 1;
		}
	} while( addedlast && total_unaccounted_leds );

	//Put the frequencies on a ring.
	j = 0;
	for( i = 0; i < sorted_map_count; i++ )
	{
		while( porpamps[i] > 0 )
		{
			ledFreqOut[j] = local_peak_freq[i];
			ledAmpOut[j] = (local_peak_amps2[i]*NOTE_FINAL_AMP)>>8;
			j++;
			porpamps[i]--;
		}
	}

	//This part totally can't run on an embedded system.
#if LIN_WRAPAROUND
	uint16_t midx = 0;
	uint32_t mqty = 100000000;
	for( j = 0; j < USE_NUM_LIN_LEDS; j++ )
	{
		uint32_t dqty;
		uint16_t localj;

		dqty = 0;
		localj = j;
		for( l = 0; l < USE_NUM_LIN_LEDS; l++ )
		{
			int32_t d = (int32_t)ledFreqOut[localj] - (int32_t)ledFreqOutOld[l];
			if( d < 0 ) d *= -1;
			if( d > (NOTERANGE>>1) ) { d = NOTERANGE - d + 1; }
			dqty += ( d * d );

			localj++;
			if( localj == USE_NUM_LIN_LEDS ) localj = 0;
		}

		if( dqty < mqty )
		{
			mqty = dqty;
			midx = j;
		}
	}

	ledSpin = midx;

#else
	ledSpin = 0;
#endif

	j = ledSpin;
	for( l = 0; l < USE_NUM_LIN_LEDS; l++, j++ )
	{
		if( j >= USE_NUM_LIN_LEDS ) j = 0;
		ledFreqOutOld[l] = ledFreqOut[j];

		uint16_t amp = ledAmpOut[j];
		if( amp > 255 ) amp = 255;
		uint32_t color = ECCtoHEX( (ledFreqOut[j]+RootNoteOffset)%NOTERANGE, 255, amp );
		ledOut[l*3+0] = ( color >> 0 ) & 0xff;
		ledOut[l*3+1] = ( color >> 8 ) & 0xff;
		ledOut[l*3+2] = ( color >>16 ) & 0xff;
	}
/*	j = ledSpin;
	for( i = 0; i < sorted_map_count; i++ )
	{
		while( porpamps[i] > 0 )
		{
			uint16_t amp = ((uint32_t)local_peak_amps2[i] * NOTE_FINAL_AMP) >> 8;
			if( amp > 255 ) amp = 255;
			uint32_t color = ECCtoHEX( local_peak_freq[i], 255, amp );
			ledOut[j*3+0] = ( color >> 0 ) & 0xff;
			ledOut[j*3+1] = ( color >> 8 ) & 0xff;
			ledOut[j*3+2] = ( color >>16 ) & 0xff;

			j++;
			if( j == USE_NUM_LIN_LEDS ) j = 0;
			porpamps[i]--;
		}
	}*/

	//Now, we use porpamps to march through the LEDs, coloring them.
/*	j = 0;
	for( i = 0; i < sorted_map_count; i++ )
	{
		while( porpamps[i] > 0 )
		{
			uint16_t amp = ((uint32_t)local_peak_amps2[i] * NOTE_FINAL_AMP) >> 8;
			if( amp > 255 ) amp = 255;
			uint32_t color = ECCtoHEX( local_peak_freq[i], 255, amp );
			ledOut[j*3+0] = ( color >> 0 ) & 0xff;
			ledOut[j*3+1] = ( color >> 8 ) & 0xff;
			ledOut[j*3+2] = ( color >>16 ) & 0xff;

			j++;
			porpamps[i]--;
		}
	}*/
}




void UpdateOff() {
	int i;
	if (needsLoad) {	
		for( i = 0; i < USE_NUM_LIN_LEDS; i++ ) {
			ledOut[i*3+0] = 0;
			ledOut[i*3+1] = 0;
			ledOut[i*3+2] = 0;
		}
		needsLoad = false;
	}
}

void UpdateSolid() {
	uint32_t color = 0;
	int i;
	if (true) {	// Load initial set of christmas colors into LED array
		for( i = 0; i < USE_NUM_LIN_LEDS; i++ ) {
			// Change this section to load the specific clors you want into the LED array
			if (i % 5 == 0) {
				color = ECCtoHEX( 0, 255, 100 ); //yellow
			} else if (i % 5 == 1) {
				color = ECCtoHEX( 43, 255, 80 ); //red
			} else if (i % 5 == 2) {
				color = ECCtoHEX( 96, 255, 80 ); // purple
			} else if (i % 5 == 3) {
				color = ECCtoHEX( 128, 255, 100 ); // blue
			} else {
				color = ECCtoHEX( 160, 255, 80 ); // green
			}

			ledOut[i*3+0] = ( color >> 0 ) & 0xff;
			ledOut[i*3+1] = ( color >> 8 ) & 0xff;
			ledOut[i*3+2] = ( color >>16 ) & 0xff;
		}
		needsLoad = false;
	}
}

void UpdateTwinkle() {
	uint8_t n = (os_random() & 0xff) % 5;
	uint32_t color = 0;
	switch(n) {
		case 0:
			color = ECCtoHEX( 0, 255, 100 ); //yellow
			break; 
		case 1:
			color = ECCtoHEX( 43, 255, 100 ); //red
			break; 
		case 2:
			color = ECCtoHEX( 96, 255, 100 ); //purple
			break; 
		case 3:
			color = ECCtoHEX( 128, 255, 100 ); //blue
			break; 
		case 4:
			color = ECCtoHEX( 160, 255, 100 ); //green
			break;
	}
	n = os_random() % USE_NUM_LIN_LEDS;
	ledOut[n*3+0] = ( color >> 0 ) & 0xff;
	ledOut[n*3+1] = ( color >> 8 ) & 0xff;
	ledOut[n*3+2] = ( color >>16 ) & 0xff;
}

void UpdateRainbow()
{
	int i;
	uint8_t freq = 0;

	if (needsLoad) {	// Load initial set of rainbow colors into LED array
		for( i = 0; i < USE_NUM_LIN_LEDS; i++ ) {
			freq = i * NOTERANGE / USE_NUM_LIN_LEDS;
			uint32_t color = ECCtoHEX( freq, 255, 175 );

			ledOut[i*3+0] = ( color >> 0 ) & 0xff;
			ledOut[i*3+1] = ( color >> 8 ) & 0xff;
			ledOut[i*3+2] = ( color >>16 ) & 0xff;
		}
		needsLoad = false;
	}

	if (system_get_time() - lastRotTime > 10000) {
		uint8_t lastLed0 = ledOut[(USE_NUM_LIN_LEDS-1)*3+0];
		uint8_t lastLed1 = ledOut[(USE_NUM_LIN_LEDS-1)*3+1];
		uint8_t lastLed2 = ledOut[(USE_NUM_LIN_LEDS-1)*3+2];
		
		for( i = USE_NUM_LIN_LEDS-1; i >= 0; i-- ) {
			ledOut[i*3+3] = ledOut[i*3+0];
			ledOut[i*3+4] = ledOut[i*3+1];
			ledOut[i*3+5] = ledOut[i*3+2];
		}

		ledOut[0] = lastLed0;
		ledOut[1] = lastLed1;
		ledOut[2] = lastLed2;

		lastRotTime = system_get_time();
	}
}

void UpdateAllSameLEDs()
{
	int i;
	uint8_t freq = 0;
	uint16_t amp = 0;

	for( i = 0; i < MAXNOTES; i++ )
	{
		uint16_t ist = note_peak_amps2[i];
		uint8_t ifrq = note_peak_freqs[i];
		if( ist > amp && ifrq != 255 )
		{
			freq = ifrq;
			amp = ist;
		}
	}

	amp = (((uint32_t)(amp))*NOTE_FINAL_AMP)>>10;

	if( amp > 255 ) amp = 255;
	uint32_t color = ECCtoHEX( (freq+RootNoteOffset)%NOTERANGE, 255, amp );

	for( i = 0; i < USE_NUM_LIN_LEDS; i++ )
	{
		ledOut[i*3+0] = ( color >> 0 ) & 0xff;
		ledOut[i*3+1] = ( color >> 8 ) & 0xff;
		ledOut[i*3+2] = ( color >>16 ) & 0xff;
	}
}






uint32_t ECCtoHEX( uint8_t note, uint8_t sat, uint8_t val )
{
	uint16_t hue = 0;
	uint16_t third = 65535/3;
	uint16_t scalednote = note;
	uint32_t renote = ((uint32_t)note * 65536) / NOTERANGE;

	//Note is expected to be a vale from 0..(NOTERANGE-1)
	//renote goes from 0 to the next one under 65536.


	if( renote < third )
	{
		//Yellow to Red.
		hue = (third - renote) >> 1;
	}
	else if( renote < (third<<1) )
	{
		//Red to Blue
		hue = (third-renote);
	}
	else
	{
		//hue = ((((65535-renote)>>8) * (uint32_t)(third>>8)) >> 1) + (third<<1);
		hue = (uint16_t)(((uint32_t)(65536-renote)<<16) / (third<<1)) + (third>>1); // ((((65535-renote)>>8) * (uint32_t)(third>>8)) >> 1) + (third<<1);
	}
	hue >>= 8;

	return EHSVtoHEX( hue, sat, val );
}

uint32_t EHSVtoHEX( uint8_t hue, uint8_t sat, uint8_t val )
{
	#define SIXTH1 43
	#define SIXTH2 85
	#define SIXTH3 128
	#define SIXTH4 171
	#define SIXTH5 213

	uint16_t or = 0, og = 0, ob = 0;

	hue -= SIXTH1; //Off by 60 degrees.

	// This supports a mode (COLORCHORD_OUTPUT_DRIVER=6) where notes are mapped to
	// 6 specific colors. These particular colors are Mardis Gras oriented.

	if ( COLORCHORD_OUTPUT_DRIVER == 6) {
		if( hue < SIXTH1 ) {
			og = 170;
		} else if( hue < SIXTH2 ) {
			or = 150;
			og = 150;
		} else if( hue < SIXTH3 ) {
			or = 95;
			ob = 95;
		} else if( hue < SIXTH4 ) {
			og = 170;
		} else if( hue < SIXTH5 ) {
			or = 150;
			og = 150;
		} else {
			or = 95;
			ob = 95;
		}
	} else {
		if( hue < SIXTH1 ) //Ok: Yellow->Red.
		{
			or = 255;
			og = 255 - ((uint16_t)hue * 255) / (SIXTH1);
		}
		else if( hue < SIXTH2 ) //Ok: Red->Purple
		{
			or = 255;
			ob = (uint16_t)hue*255 / SIXTH1 - 255;
		}
		else if( hue < SIXTH3 )  //Ok: Purple->Blue
		{
			ob = 255;
			or = ((SIXTH3-hue) * 255) / (SIXTH1);
		}
		else if( hue < SIXTH4 ) //Ok: Blue->Cyan
		{
			ob = 255;
			og = (hue - SIXTH3)*255 / SIXTH1;
		}
		else if( hue < SIXTH5 ) //Ok: Cyan->Green.
		{
			og = 255;
			ob = ((SIXTH5-hue)*255) / SIXTH1;
		}
		else //Green->Yellow
		{
			og = 255;
			or = (hue - SIXTH5) * 255 / SIXTH1;
		}
	}

	uint16_t rv = val;
	if( rv > 128 ) rv++;
	uint16_t rs = sat;
	if( rs > 128 ) rs++;

	//or, og, ob range from 0...255 now.
	//Need to apply saturation and value.

	or = (or * val)>>8;
	og = (og * val)>>8;
	ob = (ob * val)>>8;

	//OR..OB == 0..65025
	or = or * rs + 255 * (256-rs);
	og = og * rs + 255 * (256-rs);
	ob = ob * rs + 255 * (256-rs);
//printf( "__%d %d %d =-> %d\n", or, og, ob, rs );

	or >>= 8;
	og >>= 8;
	ob >>= 8;

	return og | (or<<8) | ((uint32_t)ob<<16);
}

