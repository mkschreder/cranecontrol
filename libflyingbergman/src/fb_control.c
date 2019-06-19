/** :ms-top-comment
 *  _____ _       _             ____
 * |  ___| |_   _(_)_ __   __ _| __ )  ___ _ __ __ _ _ __ ___   __ _ _ __
 * | |_  | | | | | | '_ \ / _` |  _ \ / _ \ '__/ _` | '_ ` _ \ / _` | '_ \
 * |  _| | | |_| | | | | | (_| | |_) |  __/ | | (_| | | | | | | (_| | | | |
 * |_|   |_|\__, |_|_| |_|\__, |____/ \___|_|  \__, |_| |_| |_|\__,_|_| |_|
 *          |___/         |___/                |___/
 **/
#include "fb_control.h"
#include "fb.h"

#define FB_CONTROL_CLOCK_HZ 1000

void fb_control_init(struct fb_control *self, const struct fb_config_control *conf) {
	memset(self, 0, sizeof(*self));
	self->conf = conf;
	fb_filter_init(&self->err_filt, &self->conf->error_filter);
}

void fb_control_set_input(struct fb_control *self, float pos, float vel) {
	self->input.pos = pos;
	self->input.vel = vel;
}

void fb_control_set_limits(struct fb_control *self, float acc, float speed,
                           float dec) {
	self->limits.acc = acc;
	self->limits.vel = speed;
	self->limits.dec = dec;
}

void fb_control_set_target(struct fb_control *self, float pos) {
	//printk("new target %d\n", (int32_t)(pos * 1000));
	self->new_target = pos;
	self->start = true;
	self->integral = 0;
}

static void _fb_control_run(struct fb_control *self) {
	motion_profile_get_pva(&self->trajectory, self->time, &self->target.pos,
	                       &self->target.vel, &self->target.acc);

	float err = 0;

	if(self->conf->pos_units == FB_CONTROL_POS_UNITS_RAD) {
		self->target.pos = normalize_angle(self->target.pos + self->start_pos);
		err = normalize_angle(self->target.pos - self->input.pos);
	} else {
		self->target.pos += self->start_pos;
		err = self->target.pos - self->input.pos;
	}

	err = fb_filter_update(&self->err_filt, err);

	self->integral = constrain_float(self->integral + err,
	                                 -self->conf->limits.integral_max,
	                                 self->conf->limits.integral_max);

	float derr = (err - self->error) / FB_DEFAULT_DT;
	float co = self->conf->Kff * self->target.vel + self->conf->Kp * err +
	           self->conf->Ki * self->integral + self->conf->Kd * derr;

	co = constrain_float(co, -FB_CONTROL_OUTPUT_LIMIT, FB_CONTROL_OUTPUT_LIMIT);

	self->output = co;
	self->error = err;
}

void fb_control_clock(struct fb_control *self) {
	if(self->moving && motion_profile_completed(&self->trajectory, self->time)) {
		//printk("move completed %d %d %d\n", (int32_t)(self->time * 1000),
		//       (int32_t)(self->new_target * 1000), (int32_t)(self->input.pos * 1000));
		self->moving = false;
		self->settling = true;
		self->settle_time = self->conf->settling_time;
	}

	if(self->moving) {
		self->time += FB_DEFAULT_DT;
		_fb_control_run(self);
	} else if(self->settling) {
		self->time += FB_DEFAULT_DT;
		if(self->settle_time > 0) {
			self->settle_time--;
			_fb_control_run(self);
		} else {
			//printk("control done\n");
			self->settling = false;
			self->output = 0;
			self->integral = 0;
			self->time = 0;
		}
	} else {
		self->output = 0;
		self->integral = 0;
		self->error = 0;
		self->time = 0;
	}

	// make sure we start next move in the same clock cycle as completion of previous
	// move
	if(!self->moving && !self->settling && self->start) {
		//printk("starting new move\n");
		motion_profile_init(&self->trajectory, self->limits.acc, self->limits.vel,
		                    self->limits.dec);

		float diff = self->new_target - self->input.pos;
		if(self->conf->pos_units == FB_CONTROL_POS_UNITS_RAD) {
			diff = normalize_angle(diff);
		}
		motion_profile_plan_move(&self->trajectory, 0, self->input.vel,
		                         diff, 0.f);
		self->start_pos = self->input.pos;

		self->start = false;
		self->moving = true;
		self->integral = 0;
		self->error = 0;
		self->time = 0;
	}
}

float fb_control_get_output(struct fb_control *self) {
	return self->output;
}
