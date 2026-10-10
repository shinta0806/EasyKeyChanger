function adjust_page_top_button() {
	var vPos = $(window).scrollTop();
	if( vPos > 0 ) {
		$("#PageTopButton").fadeIn("slow");
	} else {
		$("#PageTopButton").fadeOut("slow");
	}
}

$(function($) {
	$(window).load(function () {
		adjust_page_top_button();
	});
});

$(function($) {
	$(window).scroll(function () {
		adjust_page_top_button();
	});
});

