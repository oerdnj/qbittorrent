/*
 * MIT License
 * Copyright (c) 2008 Ishan Arora <ishan@qbittorrent.org>,
 * Christophe Dumez <chris@qbittorrent.org>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

myTable = new dynamicTable();
ajaxfn = function(){};
setSortedColumn = function(index){
  myTable.setSortedColumn(index);
};

window.addEvent('load', function(){
  
  var saveColumnSizes = function() {
    var filters_width = $('Filters').getSize().x;
    var properties_height = $('properties').getSize().y;
    // Save it in a cookie
    Cookie.write('filters_width', filters_width);
    Cookie.write('properties_height', properties_height);
  }
  
  /*MochaUI.Desktop = new MochaUI.Desktop();
  MochaUI.Desktop.desktop.setStyles({
	'background': '#fff',
	'visibility': 'visible'
  });*/
  MochaUI.Desktop.initialize();

  var filt_w = Cookie.read('filters_width');
  if($defined(filt_w))
     filt_w =  filt_w.toInt();
  else
    filt_w = 120;
  new MochaUI.Column({
		id: 'filtersColumn',
		placement: 'left',
		onResize: saveColumnSizes,
		width: filt_w,
		resizeLimit: [100, 300]
	});
  new MochaUI.Column({
		id: 'mainColumn',
		placement: 'main',	
		width: null,
		resizeLimit: [100, 300]
	});
  MochaUI.Desktop.setDesktopSize();
  new MochaUI.Panel({
		id: 'Filters',
		title: 'Panel',
		header: false,
		padding: { top: 0, right: 0, bottom: 0, left: 0 },
		loadMethod: 'xhr',
		contentURL: 'filters.html',
		column: 'filtersColumn',
		height: 300
	});
    new MochaUI.Panel({
		id: 'transferList',
		title: 'Panel',
		header: false,
		padding: { top: 0, right: 0, bottom: 0, left: 0 },
		loadMethod: 'xhr',
		contentURL: 'transferlist.html',
		column: 'mainColumn',
		onResize: saveColumnSizes,
		height: null
	});
    var prop_h = Cookie.read('properties_height');
    if($defined(prop_h))
      prop_h = prop_h.toInt();
    else
      prop_h = 200;
    new MochaUI.Panel({
		id: 'properties',
		title: 'Panel',
		header: true,
		padding: { top: 0, right: 0, bottom: 0, left: 0 },
		contentURL: 'prop-general.html',
		require: {
                  css: ['css/Tabs.css']
                },
                tabsURL: 'properties.html',
		column: 'mainColumn',
		height: prop_h
	});
  initializeWindows();
  var r=0;
  var waiting=false;
  
  var stateToImg = function(state){
    return 'images/skin/'+state+'.png';
  };
	var ajaxfn = function(){
		var queueing_enabled = false;
		var url = 'json/events';
		if (!waiting){
			waiting=true;
			var request = new Request.JSON({
                                url: url,
				noCache: true,
				method: 'get',
				onFailure: function() {
					$('error_div').set('html', 'qBittorrent client is not reachable');
					waiting=false;
					ajaxfn.delay(2000);
				},
				onSuccess: function(events) {
					 $('error_div').set('html', '');
					if(events){
            // Add new torrents or update them
            torrent_hashes = myTable.getRowIds();
            events_hashes = new Array();
            events.each(function(event){
              events_hashes[events_hashes.length] = event.hash;
                var row = new Array();
                row.length = 10;
                row[0] = stateToImg(event.state);
                row[1] = event.name;
		row[2] = event.priority
                row[3] = event.size;
                row[4] = (event.progress*100).round(1);
		row[5] = event.num_seeds;
		row[6] = event.num_leechs;
                row[7] = event.dlspeed;
                row[8] = event.upspeed;
		row[9] = event.eta;
		row[10] = event.ratio;
		if(row[2] != "*")
			queueing_enabled = true;
               if(!torrent_hashes.contains(event.hash)) {
                  // New unfinished torrent
                  torrent_hashes[torrent_hashes.length] = event.hash;
		  //alert("Inserting row");
                  myTable.insertRow(event.hash, row, event.state);
                } else {
                  // Update torrent data
                  myTable.updateRow(event.hash, row, event.state);
                }
            });
            // Remove deleted torrents
            torrent_hashes.each(function(hash){
              if(!events_hashes.contains(hash)) {
                myTable.removeRow(hash);
              }
            });
	    if(queueing_enabled) {
		$('queueingButtons').removeClass('invisible');
		myTable.showPriority();
	    } else {
		$('queueingButtons').addClass('invisible');
		myTable.hidePriority();
	    }
					}
					waiting=false;
					ajaxfn.delay(1500);
				}
			}).send();
		}
	};
	ajaxfn();
// 	ajaxfn.periodical(5000);

	setFilter = function(f) {
	  // Visually Select the right filter
	  $("all_filter").removeClass("selectedFilter");
	  $("downloading_filter").removeClass("selectedFilter");
	  $("completed_filter").removeClass("selectedFilter");
	  $("active_filter").removeClass("selectedFilter");
	  $("inactive_filter").removeClass("selectedFilter");
	  $(f+"_filter").addClass("selectedFilter");
	  myTable.setFilter(f);
	  ajaxfn();
	  // Remember this via Cookie
	  Cookie.write('selected_filter', f);
	}

});

function closeWindows() {
  MochaUI.closeAll();
}

// This runs when a person leaves your page.

//window.addEvent('unload', function(){
//	if (MochaUI && Browser.Engine.trident != true) {
//	  MochaUI.garbageCleanUp();
//	}
//});

window.addEvent('keydown', function(event){
  if (event.key == 'a' && event.control) {
    event.stop();
    myTable.selectAll();
  }
});
