/**
 * Marlin 3D Printer Firmware
 * Copyright (C) 2016 MarlinFirmware [https://github.com/MarlinFirmware/Marlin]
 *
 * Based on Sprinter and grbl.
 * Copyright (C) 2011 Camiel Gubbels / Erik van der Zalm
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

/**
 *
 * About Marlin
 *
 * This firmware is a mashup between Sprinter and grbl.
 *  - https://github.com/kliment/Sprinter
 *  - https://github.com/simen/grbl/tree
 *
 * It has preliminary support for Matthew Roberts advance algorithm
 *  - http://reprap.org/pipermail/reprap-dev/2011-May/003323.html
 */

#include "Marlin.h"

#if ENABLED(UNIFIED_BED_LEVELING_FEATURE)
//#include "vector_3.h"
//#include "qr_solve.h"

#include "Bed_Leveling.h"
//#include "configuration_store.h"
#include "planner.h"
#include <avr/io.h>
#include <math.h>


void wait_for_button_press();

void mesh_buffer_line(float x_end, float y_end, float z_end, float e_end, float feed_rate, unsigned char extruder) {

	int cell_start_xi, cell_start_yi, cell_dest_xi, cell_dest_yi;
	int left_flag, down_flag;
	int current_xi, current_yi;
	int dxi, dyi, xi_cnt, yi_cnt;
	bool use_X_dist, inf_normalized_flag, inf_m_flag;
	float x_start, y_start;
	float x, y, z1, z2, z0;
	float next_mesh_line_x, next_mesh_line_y, a0ma1diva2ma1; 
	float on_axis_distance, e_normalized_dist, e_position, e_start, z_normalized_dist, z_position, z_start;
	float dx, dy, adx, ady, m, c;

	//
	// Much of the nozzle movement will be within the same cell.  So we will do as little computation
	// as possible to determine if this is the case.  If this move is within the same cell, we will
	// just do the required Z-Height correction, call the Planner's buffer_line() routine, and leave
	//
	
	x_start = current_position[X_AXIS];
	y_start = current_position[Y_AXIS];
	z_start = current_position[Z_AXIS];
	e_start = current_position[E_AXIS];

	cell_start_xi = blm.get_cell_index_x(x_start);
	cell_start_yi = blm.get_cell_index_y(y_start);
	cell_dest_xi  = blm.get_cell_index_x(x_end);
	cell_dest_yi  = blm.get_cell_index_y(y_end);

	if ((cell_start_xi == cell_dest_xi) && (cell_start_yi == cell_dest_yi)) {	// if the whole move is within the same cell, 
											// we don't need to break up the move
		//
		// If we are moving off the print bed, we are going to allow the move at this level.
		// But we detect it and isolate it.   For now, we just pass along the request.
		//

		if (cell_dest_xi<0 || cell_dest_yi<0 || cell_dest_xi >= MESH_NUM_X_POINTS || cell_dest_yi >= MESH_NUM_Y_POINTS) {

			// Note:  There is no Z Correction in this case.  We are off the grid and don't know what
			// a reasonable correction would be.

			planner.buffer_line(x_end, y_end, z_end + blm.state.z_offset, e_end, feed_rate, extruder);
			set_current_to_destination();
			return;
		}

		// we can optimize some floating point operations here.  We could call float get_z_correction(float x0, float y0) to
		// generate the correction for us.  But we can lighten the load on the CPU by doing a modified version of the function.
		// We are going to only calculate the amount we are from the first mesh line towards the second mesh line once.  
		// We will use this fraction in both of the original two Z Height calculations for the bi-linear interpolation.  And, 
		// instead of doing a generic divide of the distance, we know the distance is MESH_X_DIST so we can use the preprocessor 
		// to create a 1-over number for us.  That will allow us to do a floating point multiply instead of a floating point divide.

	FINAL_MOVE:
		a0ma1diva2ma1 = (x_end - mesh_index_to_X_location[cell_dest_xi]) * (float) (1.0 / MESH_X_DIST);
		z1 = z_values[cell_dest_xi][cell_dest_yi] +
			(z_values[cell_dest_xi + 1][cell_dest_yi] - z_values[cell_dest_xi][cell_dest_yi]) * a0ma1diva2ma1;
		z2 = z_values[cell_dest_xi + 1][cell_dest_yi + 1] +
			(z_values[cell_dest_xi][cell_dest_yi + 1] - z_values[cell_dest_xi + 1][cell_dest_yi + 1]) * a0ma1diva2ma1;

		// we are done with the fractional X distance into the cell.  Now with the two Z-Heights we have calculated, we 
		// are going to apply the Y-Distance into the cell to interpolate the final Z correction.

		a0ma1diva2ma1 = (y_end - mesh_index_to_Y_location[cell_dest_yi]) * (float) (1.0 / MESH_Y_DIST);

		z0 = z1 + (z2 - z1) * a0ma1diva2ma1;
		z0 = z0 * blm.fade_scaling_factor_for_Z( z_end );

		if (isnan(z0)) {	// if part of the Mesh is undefined, it will show up as NAN
			z0 = 0.0;	// in z_values[][] and propagate through the 
					// calculations. If our correction is NAN, we throw it out 
					// because part of the Mesh is undefined and we don't have the 
					// information we need to complete the height correction.
		}
		planner.buffer_line(x_end, y_end, z_end + z0 + blm.state.z_offset, e_end, feed_rate, extruder);
		set_current_to_destination();
		return;
	}

	//	
	//	If we get here, we are processing a move that crosses at least one Mesh Line.   We will check
	//	for the simple case of just crossing X or just crossing Y Mesh Lines after we get all the details
	//	of the move figured out.  We can process the easy case of just crossing an X or Y Mesh Line with less 
	//	computation and in fact most lines are of this nature.  We will check for that in the following
	//	blocks of code:

	left_flag = 0;
	down_flag = 0;
	inf_m_flag = false;
	inf_normalized_flag = false;

	dx = x_end - x_start;
	dy = y_end - y_start;
	
	if (dx<0.0) { 		// figure out which way we need to move to get to the next cell
		dxi = -1; 
		adx = -dx;	// absolute value of dx.  We already need to check if dx and dy are negative. 
	} else {		// We may as well generate the appropriate values for adx and ady right now
		dxi = 1;	// to save setting up the abs() function call and actually doing the call.
		adx = dx;
	}
	if (dy<0.0) {
		dyi = -1; 
		ady = -dy;	// absolute value of dy
	} else {
		dyi = 1;
		ady = dy;
	}

	if (dx<0.0) left_flag = 1;
	if (dy<0.0) down_flag = 1;
	if (cell_start_xi == cell_dest_xi) dxi = 0;
	if (cell_start_yi == cell_dest_yi) dyi = 0;

//
// Compute the scaling factor for the extruder for each partial move.
// We need to watch out for zero length moves because it will cause us to
// have an infinate scaling factor.  We are stuck doing a floating point 
// divide to get our scaling factor, but after that, we just multiply by this
// number.   We also pick our scaling factor based on whether the X or Y 
// component is larger.  We use the biggest of the two to preserve precision.
//
	if ( adx > ady ) {
		use_X_dist = true;
		on_axis_distance   = x_end-x_start;
	}
	else {
		use_X_dist = false;
		on_axis_distance   = y_end-y_start;
	}
	e_position = e_end - e_start;
	e_normalized_dist = e_position / on_axis_distance; 

	z_position = z_end - z_start;
	z_normalized_dist = z_position / on_axis_distance; 

	if (e_normalized_dist==INFINITY || e_normalized_dist==-INFINITY)
		inf_normalized_flag = true;
	current_xi = cell_start_xi;
	current_yi = cell_start_yi;

	m = dy / dx;
	c = y_start - m*x_start;
	if (m == INFINITY || m == -INFINITY)
		inf_m_flag = true;
	// 
	// This block handles vertical lines.  These are lines that stay within the same
	// X Cell column.  They do not need to be perfectly vertical.  They just can
	// not cross into another X Cell column.
	//
	if (dxi == 0) {				// Check for a vertical line
		current_yi += down_flag;	// Line is heading down, we just want to go to the bottom
		while (current_yi != cell_dest_yi + down_flag) {
			current_yi += dyi;
			next_mesh_line_y = mesh_index_to_Y_location[current_yi];
			if (inf_m_flag)
				x = x_start;		// if the slope of the line is infinite, we won't do the calculations
						// we know the next X is the same so we can recover and continue!
			else
				x = (next_mesh_line_y - c) / m;	// Calculate X at the next Y mesh line

			z0 = blm.get_z_correction_along_horizontal_mesh_line_at_specific_X(x, current_xi, current_yi);
			z0 = z0 * blm.fade_scaling_factor_for_Z( z_end );

			if (isnan(z0)) {	// if part of the Mesh is undefined, it will show up as NAN
				z0 = 0.0;	// in z_values[][] and propagate through the 
						// calculations. If our correction is NAN, we throw it out 
						// because part of the Mesh is undefined and we don't have the 
						// information we need to complete the height correction.
			}
			y = mesh_index_to_Y_location[current_yi];

	// Without this check, it is possible for the algorythm to generate a zero length move in the case 
	// where the line is heading down and it is starting right on a Mesh Line boundary.  For how often that 
	// happens, it might be best to remove the check and always 'schedule' the move because 
	// the planner.buffer_line() routine will filter it if that happens.
			if ( y!=y_start)	 {
			  	if ( inf_normalized_flag == false ) {
					on_axis_distance   = y - y_start;				// we don't need to check if the extruder position
					e_position = e_start + on_axis_distance * e_normalized_dist;	// is based on X or Y because this is a vertical move
					z_position = z_start + on_axis_distance * z_normalized_dist;	
				} else {
					e_position = e_start;
					z_position = z_start;
				}

				planner.buffer_line(x, y, z_position + z0 + blm.state.z_offset, e_position, feed_rate, extruder);
			} //else printf("FIRST MOVE PRUNED  ");
		}
		//
		// Check if we are at the final destination.  Usually, we won't be, but if it is on a Y Mesh Line, we are done.
		//
		if (current_position[X_AXIS] != x_end || current_position[Y_AXIS] != y_end)
			goto FINAL_MOVE;
		set_current_to_destination();
		return;
	}

	// 
	// This block handles horizontal lines.  These are lines that stay within the same
	// Y Cell row.  They do not need to be perfectly horizontal.  They just can
	// not cross into another Y Cell row.
	//

	if (dyi == 0) {				// Check for a horiziontal line
		current_xi += left_flag;	// Line is heading left, we just want to go to the left
						// edge of this cell for the first move.
		while (current_xi != cell_dest_xi + left_flag) {
			current_xi += dxi;
			next_mesh_line_x = mesh_index_to_X_location[current_xi];
			y = m * next_mesh_line_x + c;		// Calculate X at the next Y mesh line

			z0 = blm.get_z_correction_along_vertical_mesh_line_at_specific_Y(y, current_xi, current_yi);
			z0 = z0 * blm.fade_scaling_factor_for_Z( z_end );

			if (isnan(z0)) {	// if part of the Mesh is undefined, it will show up as NAN
				z0 = 0.0;	// in z_values[][] and propagate through the 
						// calculations. If our correction is NAN, we throw it out 
						// because part of the Mesh is undefined and we don't have the 
						// information we need to complete the height correction.
			}
			x = mesh_index_to_X_location[current_xi];

	// Without this check, it is possible for the algorythm to generate a zero length move in the case 
	// where the line is heading left and it is starting right on a Mesh Line boundary.  For how often 
	// that happens, it might be best to remove the check and always 'schedule' the move because 
	// the planner.buffer_line() routine will filter it if that happens.
			if ( x!=x_start)	 {
			  	if ( inf_normalized_flag == false ) {
					on_axis_distance   = x - x_start;				// we don't need to check if the extruder position
					e_position = e_start + on_axis_distance * e_normalized_dist;	// is based on X or Y because this is a horizontal move
					z_position = z_start + on_axis_distance * z_normalized_dist;	
				} else {
					e_position = e_start;
					z_position = z_start;
				}

				planner.buffer_line(x, y, z_position + z0 + blm.state.z_offset, e_position, feed_rate, extruder);
			  } //else printf("FIRST MOVE PRUNED  ");
		}
		if (current_position[X_AXIS] != x_end || current_position[Y_AXIS] != y_end)
			goto FINAL_MOVE;
		set_current_to_destination();
		return;
	}


//
//
//
// 
// This block handles the generic case of a line crossing both X and Y
// Mesh lines.
//
//
//
//

	xi_cnt = cell_start_xi - cell_dest_xi;
	if ( xi_cnt < 0 ) 
		xi_cnt = -xi_cnt;

	yi_cnt = cell_start_yi - cell_dest_yi;
	if ( yi_cnt < 0 ) 
		yi_cnt = -yi_cnt;

	current_xi += left_flag;
	current_yi += down_flag;

	while ( xi_cnt>0 || yi_cnt>0 )		{

		next_mesh_line_x = mesh_index_to_X_location[current_xi + dxi];
		next_mesh_line_y = mesh_index_to_Y_location[current_yi + dyi];

		y = m * next_mesh_line_x + c;	// Calculate Y at the next X mesh line
		x = (next_mesh_line_y-c) / m;	// Calculate X at the next Y mesh line    (we don't have to worry
						// about m being equal to 0.0  If this was the case, we would have
						// detected this as a vertical line move up above and we wouldn't
						// be down here doing a generic type of move.

		if ((left_flag && (x>next_mesh_line_x)) || (!left_flag && (x<next_mesh_line_x))) { // Check if we hit the Y line first
//
// Yes!  Crossing a Y Mesh Line next
//
			z0 = blm.get_z_correction_along_horizontal_mesh_line_at_specific_X(x, current_xi-left_flag, current_yi+dyi); 

			z0 = z0 * blm.fade_scaling_factor_for_Z( z_end );
			if (isnan(z0)) {	// if part of the Mesh is undefined, it will show up as NAN
				z0 = 0.0;	// in z_values[][] and propagate through the 
						// calculations. If our correction is NAN, we throw it out 
						// because part of the Mesh is undefined and we don't have the 
						// information we need to complete the height correction.
			}

			if ( inf_normalized_flag == false ) {
				if ( use_X_dist )  
					on_axis_distance   = x - x_start;
				 else 
					on_axis_distance   = next_mesh_line_y - y_start;
				e_position = e_start + on_axis_distance * e_normalized_dist;
				z_position = z_start + on_axis_distance * z_normalized_dist;	
			} else {
				e_position = e_start;
				z_position = z_start;
			}
			planner.buffer_line(x, next_mesh_line_y, z_position + z0 + blm.state.z_offset, e_position, feed_rate, extruder);
			current_yi += dyi;
			yi_cnt--;
		}
		else {	
//
// Yes!  Crossing a X Mesh Line next
//
			z0 = blm.get_z_correction_along_vertical_mesh_line_at_specific_Y(y, current_xi+dxi, current_yi-down_flag);

			z0 = z0 * blm.fade_scaling_factor_for_Z( z_end );

			if (isnan(z0)) {	// if part of the Mesh is undefined, it will show up as NAN
				z0 = 0.0;	// in z_values[][] and propagate through the 
						// calculations. If our correction is NAN, we throw it out 
						// because part of the Mesh is undefined and we don't have the 
						// information we need to complete the height correction.
			}
			if ( inf_normalized_flag == false ) {
				if ( use_X_dist )  
					on_axis_distance   = next_mesh_line_x - x_start;
				 else 
					on_axis_distance   = y - y_start;
				e_position = e_start + on_axis_distance * e_normalized_dist;
				z_position = z_start + on_axis_distance * z_normalized_dist;
			} else {
				e_position = e_start;
				z_position = z_start;
			}

			planner.buffer_line(next_mesh_line_x, y, z_position + z0 + blm.state.z_offset, e_position, feed_rate, extruder);
			current_xi += dxi;
			xi_cnt--;
		}
	}
	if (current_position[0] != x_end || current_position[1] != y_end)  {
		goto FINAL_MOVE;
	}
	set_current_to_destination();
	return;
}



void wait_for_button_press() {
//	if ( !been_to_2_6 ) 
		return;

	pinMode(66, INPUT_PULLUP); // Roxy's Left Switch is on pin 66.  Right Switch is on pin 65
	pinMode(64, OUTPUT );  
	while ( (digitalRead(66) & 0x01) != 0)  
		idle();

	delay(50);
	while ( (digitalRead(66) & 0x01) == 0)  {
		idle();
	}
	delay(50);

	return;
}

#endif


